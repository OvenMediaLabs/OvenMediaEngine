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
		// `PushProvider::OnChannelDeleted()` calls this for a channel that never joined an application.
		// The default is a no-op: a provider may share this transport or tear it down elsewhere.
		virtual void CloseTransport() {}

		// Enters and leaves `OnDataReceived()` on this channel.
		// The time it spends there is not client silence,
		// so `PushProvider::ChannelTaskRunner()` passes over a channel that is inside it.
		// `<AdmissionWebhooks><Timeout>` does not bound that time.
		// Entering fails once `TryBeginReaping()` has succeeded, and the caller then drops the data.
		// These count rather than set a flag, because two calls for one channel may overlap.
		bool BeginProcessingData();
		void EndProcessingData();
		bool IsProcessingData();

		// channel may not yet determined, so we manage the timer separately
		void UpdateLastReceivedTime();
		void SetPacketSilenceTimeoutMs(time_t timeout_ms);
		time_t GetPacketSilenceTimeoutMs();
		time_t GetElapsedMsSinceLastReceived();

		// One read of each field `PushProvider::ChannelTaskRunner()` decides by, because pairing an old
		// elapsed time with a new `PacketSilenceTimeoutMs` deletes a live channel.
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

		// Reserves this channel for deletion, on the state `GetSilenceState()` returned.
		// One compare-and-swap decides it, and a caller that loses changes nothing:
		// it fails while `OnDataReceived()` is inside the channel, when another caller got here first,
		// or when anything that state was read from has moved.
		// Once it succeeds, `BeginProcessingData()` refuses every later call.
		bool TryBeginReaping(const SilenceState &state);

		// Applies the `PacketSilenceTimeoutMs` configured for the resolved application, which providers
		// call as soon as that application is known.
		// An option the operator did not set leaves the channel-creation default in place.
		void ApplyConfiguredPacketSilenceTimeoutMs(const info::VHostAppName &vhost_app_name);

		// Sizes the wait for an input's first coded frame and starts it, when the operator set
		// `FirstMediaWaitTimeoutMs`. Without that option no wait runs,
		// and this applies `PacketSilenceTimeoutMs` instead.
		void ApplyConfiguredFirstMediaWaitTimeoutMs(const info::VHostAppName &vhost_app_name);

		// Ends that wait. A provider calls this for a message holding a coded frame, never for a codec
		// description, which an encoder can send long before its first frame.
		// The channel is still unpublished, so the timeout becomes a positive `PacketSilenceTimeoutMs`
		// the operator set, or the channel-creation timeout otherwise.
		// `PushApplication::JoinStream()` applies the configured value, `0` included, at publish.
		void EndFirstMediaWait(const info::VHostAppName &vhost_app_name);

		// Whether that wait is running.
		// A caller can ask this first, and skip working out whether a message holds a coded frame.
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
		// Where this channel stands in the wait for its first coded frame.
		// `NotStarted` and `Ended` are distinct, because a media message can arrive before it starts.
		enum class FirstMediaWaitPhase : uint8_t
		{
			NotStarted,
			Waiting,
			Ended
		};
		std::atomic<FirstMediaWaitPhase> _first_media_wait_phase = FirstMediaWaitPhase::NotStarted;

		// Everything `TryBeginReaping()`'s one compare-and-swap has to check, in one word: how many
		// `OnDataReceived()` calls are inside this channel, a counter bumped by every write
		// `GetSilenceState()` reads, and the flag that reserves the channel for deletion.
		std::atomic<uint64_t> _activity_state					 = 0;

		// Push Provider
		std::shared_ptr<PushProvider>	_provider;
	};
}
