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

		// Marks this channel's own data handler as entered and left, for the channel task runner to skip.
		// A handler's own duration is not client silence, and `<AdmissionWebhooks><Timeout>` is unbounded.
		// These count instead of setting a flag, because two handlers for one channel may overlap.
		void BeginProcessingData();
		void EndProcessingData();
		bool IsProcessingData();

		// channel may not yet determined, so we manage the timer separately
		void UpdateLastReceivedTime();
		void SetPacketSilenceTimeoutMs(time_t timeout_ms);
		time_t GetPacketSilenceTimeoutMs();
		time_t GetElapsedMsSinceLastReceived();

		// One view of everything the channel task runner judges a channel by.
		// Field-by-field reads let a handler change some of them in between,
		// and a judgment pairing an old elapsed time with a new budget deletes a live channel.
		struct SilenceState
		{
			uint64_t generation = 0;
			time_t timeout_ms	= 0;
			time_t elapsed_ms	= -1;
			bool is_processing	= false;

			// Whether this view says the channel has been silent for longer than it may be.
			// A `0` budget disables the judgment, and a negative elapsed time means nothing has arrived.
			bool IsSilentBeyondTimeout() const
			{
				return (is_processing == false) && (timeout_ms > 0) && (elapsed_ms > timeout_ms);
			}
		};

		SilenceState GetSilenceState();

		// Whether anything `GetSilenceState()` read has moved since it was read.
		// A judgment asks this before deleting, so one made across a handler is dropped and retried.
		bool HasSilenceStateChangedSince(const SilenceState &state);

		// Apply the `PacketSilenceTimeoutMs` configured for the resolved application.
		// Concrete providers call this as soon as the application is known, which for RTMP, MPEG-TS
		// and SRT is while the channel is still unpublished, and for WebRTC is right after
		// `OnChannelCreated()` overwrote the value with the channel-creation default.
		// An option the operator did not set leaves that default in place.
		void ApplyConfiguredPacketSilenceTimeoutMs(const info::VHostAppName &vhost_app_name);

		// Sizes the wait that ends with the first media packet, and starts it.
		// `FirstMediaWaitTimeoutMs` governs the budget when the operator sized it,
		// otherwise an operator-configured `PacketSilenceTimeoutMs` does, and failing that its default.
		void ApplyConfiguredFirstMediaWaitTimeoutMs(const info::VHostAppName &vhost_app_name);

		// Ends the wait `ApplyConfiguredFirstMediaWaitTimeoutMs()` started, on the first media packet.
		// The channel is still unpublished: a positive `PacketSilenceTimeoutMs` set by the operator wins,
		// and everything else falls back to the channel-creation budget.
		// `PushApplication::JoinStream()` applies the effective value, `0` included, at publish.
		// Only the first media packet after the wait started has an effect: an earlier one does nothing.
		void EndFirstMediaWait(const info::VHostAppName &vhost_app_name);

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
		uint32_t 		_channel_id = 0;
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
		// How many of this channel's data handlers are running
		std::atomic<int32_t> _processing_data_count				 = 0;
		// Bumped by every write a silence judgment depends on, so it can tell its own read is stale
		std::atomic<uint64_t> _state_generation					 = 0;

		// Push Provider
		std::shared_ptr<PushProvider>	_provider;
	};
}
