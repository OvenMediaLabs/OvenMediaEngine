//==============================================================================
//
//  PushProvider Stream Base Class 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "base/provider/stream.h"

namespace pvd
{
	class PushProvider;
	
	class PushStream : public Stream
	{
	public:
		enum class PushStreamType : uint8_t
		{
			UNKNOWN,
			SIGNALLING, 
			DATA,
			INTERLEAVED
		};

		bool Terminate() override;

		virtual bool OnDataReceived(const std::shared_ptr<const ov::Data> &data) = 0;
		uint32_t GetChannelId();
		bool DoesBelongApplication();
		virtual PushStreamType GetPushStreamType() = 0;

		uint32_t GetRelatedChannelId();
		void SetRelatedChannelId(uint32_t related_channel_id);

		bool IsReadyToReceiveStreamData();
		bool IsPublished();

		// Closes the transport this channel owns, without the teardown `Stop()` performs.
		// `PushProvider::OnChannelDeleted()` calls this for a channel that never joined an application,
		// where nothing else would tell the client it is no longer streaming.
		// The default is a no-op, because a provider may share this transport or tear it down elsewhere.
		virtual void CloseTransport() {}

		// Enters and leaves `OnDataReceived()` on this channel.
		// Entering fails only once `TryBeginReaping()` has succeeded on this channel,
		// and the caller then has to drop the data.
		// A `TryBeginReaping()` that fails never refuses an `OnDataReceived()`,
		// because it changes nothing when it loses.
		// The time `OnDataReceived()` spends is not client silence,
		// and `<AdmissionWebhooks><Timeout>` does not bound it,
		// so `PushProvider::ChannelTaskRunner()` passes over a channel that is inside it.
		// These count rather than set a flag, because two calls for one channel may overlap.
		bool BeginProcessingData();
		void EndProcessingData();
		bool IsProcessingData();

		// channel may not yet determined, so we manage the timer separately
		void UpdateLastReceivedTime();
		void SetPacketSilenceTimeoutMs(time_t timeout_ms);
		time_t GetPacketSilenceTimeoutMs();
		time_t GetElapsedMsSinceLastReceived();

		// One read of each field `PushProvider::ChannelTaskRunner()` decides by.
		// Reading them one at a time lets `OnDataReceived()` change some of them in between,
		// and pairing an old elapsed time with a new `PacketSilenceTimeoutMs` deletes a live channel.
		// This is not an atomic snapshot: `TryBeginReaping()` is what makes acting on it safe.
		struct SilenceState
		{
			// The activity word exactly as it was read, which is what `TryBeginReaping()` compares against
			uint64_t activity  = 0;
			time_t timeout_ms  = 0;
			time_t elapsed_ms  = -1;
			bool is_processing = false;

			// Whether this view says the channel has been silent for longer than it may be.
			// A `0` timeout disables the check, and a negative elapsed time means nothing has arrived.
			bool IsSilentBeyondTimeout() const
			{
				return (is_processing == false) && (timeout_ms > 0) && (elapsed_ms > timeout_ms);
			}
		};

		SilenceState GetSilenceState();

		// Reserves this channel for deletion by the caller, on the state `GetSilenceState()` returned.
		// One compare-and-swap decides it, so a caller that loses changes nothing at all:
		// it fails while `OnDataReceived()` is inside the channel, when another caller got here first,
		// or when anything that state was read from has moved since.
		// Once it has succeeded, `BeginProcessingData()` refuses every later call, so nothing moves.
		bool TryBeginReaping(const SilenceState &state);

		// Apply the `PacketSilenceTimeoutMs` configured for the resolved application.
		// Concrete providers call this as soon as the application is known, which for RTMP, MPEG-TS
		// and SRT is while the channel is still unpublished, and for WebRTC is right after
		// `OnChannelCreated()` overwrote the value with the channel-creation default.
		// An option the operator did not set leaves that default in place.
		void ApplyConfiguredPacketSilenceTimeoutMs(const info::VHostAppName &vhost_app_name);

		// Sizes the wait for an input's first coded frame, and starts it.
		// `FirstMediaWaitTimeoutMs` governs the wait, and the option is off unless the operator set it.
		// With it absent this applies `PacketSilenceTimeoutMs` and never starts the wait,
		// so the window keeps the policy it had before the option existed.
		void ApplyConfiguredFirstMediaWaitTimeoutMs(const info::VHostAppName &vhost_app_name);

		// Ends the wait `ApplyConfiguredFirstMediaWaitTimeoutMs()` started.
		// A provider calls this for a message carrying a coded frame, never for a codec description:
		// an encoder can send the sequence header long before its first frame,
		// and ending the wait on that header would leave the wait doing nothing.
		// The channel is still unpublished: a positive `PacketSilenceTimeoutMs` set by the operator wins,
		// and everything else falls back to the channel-creation timeout.
		// `PushApplication::JoinStream()` applies the effective value, `0` included, at publish.
		// Only the first such frame after the wait started has an effect: an earlier one does nothing.
		void EndFirstMediaWait(const info::VHostAppName &vhost_app_name);

		// Whether the wait above is running.
		// A provider asks this before working out whether a message carries a frame,
		// so nothing is spent on that question while the option is off.
		bool IsWaitingForFirstMedia() const;

		uint32_t GetNumberOfAttempsToPublish()
		{
			return _attemps_publish_count;
		}

	protected:
		PushStream(StreamSourceType source_type, ov::String channel_name, uint32_t channel_id, const std::shared_ptr<PushProvider> &provider);
		PushStream(StreamSourceType source_type, ov::String channel_name, const std::shared_ptr<PushProvider> &provider);
		PushStream(StreamSourceType source_type, uint32_t channel_id, const std::shared_ptr<PushProvider> &provider);
		PushStream(StreamSourceType source_type, const std::shared_ptr<PushProvider> &provider);

		// app name, stream name, tracks
		// provider->AssignStream (app)
		// app-> NotifyStreamReady(this)
		bool PublishChannel(const info::VHostAppName &vhost_app_name);

		std::shared_ptr<PushProvider> GetProvider()
		{
			return _provider;
		}

		template <typename T>
		std::enable_if_t<std::is_base_of<PushProvider, T>::value, std::shared_ptr<T>> GetProviderAs()
		{
			return std::dynamic_pointer_cast<T>(_provider);
		}

		DirectionType GetDirectionType() override
		{
			return DirectionType::PUSH;
		}

	private:
		// Records that something `GetSilenceState()` reads has changed
		void CountStateChange();

		uint32_t _channel_id						   = 0;
		// If it's type is DATA, related channel is Signalling, or vice versa.
		uint32_t		_related_channel_id = 0; 
		// Published?
		bool			_is_published = false;
		// Time elapsed since the last OnDataReceived function was called
		std::atomic<int64_t> _last_received_time_ms = -1;
		std::atomic<time_t> _packet_silence_timeout_ms = 0;

		std::atomic<uint32_t> _attemps_publish_count   = 0;
		// Where this channel stands in the wait for its first media packet.
		// `NotStarted` and `Ended` are distinct, because a media message can arrive before it starts.
		enum class FirstMediaWaitPhase : uint8_t
		{
			NotStarted,
			Waiting,
			Ended
		};
		std::atomic<FirstMediaWaitPhase> _first_media_wait_phase = FirstMediaWaitPhase::NotStarted;

		// Everything `TryBeginReaping()` has to agree on, in one word, so that its one compare-and-swap
		// can check all of it: how many `OnDataReceived()` calls are inside this channel, a counter
		// bumped by every write `GetSilenceState()` reads, and the flag that reserves it for deletion.
		std::atomic<uint64_t> _activity_state					 = 0;

		// Push Provider
		std::shared_ptr<PushProvider>	_provider;
	};
}
