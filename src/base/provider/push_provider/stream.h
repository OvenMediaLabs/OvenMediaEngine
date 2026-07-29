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

		// Closes the transport this channel owns, without any of the teardown `Stop()` performs.
		// `PushProvider::OnChannelDeleted()` calls this for a channel that never joined an
		// application, where `Application::DeleteStream()` and therefore `Stop()` never run and
		// nothing else would let the client know it is no longer streaming.
		// The default is a no-op: a provider whose transport is shared between channels, or is torn
		// down elsewhere, must not close it here.
		virtual void CloseTransport() {}

		// Marks this channel's own data handler as entered and left.
		// The channel task runner must not judge a channel whose handler is running:
		// the time the handler spends is not client silence,
		// and access control can block it for seconds because `<AdmissionWebhooks><Timeout>`
		// has no upper bound. These count rather than set a flag, because nothing in this base class
		// guarantees that two handlers for one channel never overlap, and the first one to leave
		// must not clear the mark the other still needs.
		void BeginProcessingData();
		void EndProcessingData();
		bool IsProcessingData();

		// channel may not yet determined, so we manage the timer separately
		void UpdateLastReceivedTime();
		void SetPacketSilenceTimeoutMs(time_t timeout_ms);
		time_t GetPacketSilenceTimeoutMs();
		time_t GetElapsedMsSinceLastReceived();

		// Apply the `PacketSilenceTimeoutMs` configured for the resolved application.
		// Concrete providers call this as soon as the application is known, which for RTMP, MPEG-TS
		// and SRT is while the channel is still unpublished, and for WebRTC is right after
		// `OnChannelCreated()` overwrote the value with the channel-creation default.
		// An option the operator did not set leaves that default in place.
		void ApplyConfiguredPacketSilenceTimeoutMs(const info::VHostAppName &vhost_app_name);

		// Set the silence budget for the wait that ends with the first media packet.
		// A provider calls this when that wait begins. `FirstMediaWaitTimeoutMs` governs it
		// when the operator sized it; otherwise an operator-configured `PacketSilenceTimeoutMs` does,
		// and failing that the default for this wait.
		// So this call decides the budget on its own and needs no second call.
		void ApplyConfiguredFirstMediaWaitTimeoutMs(const info::VHostAppName &vhost_app_name);

		// End the wait that `ApplyConfiguredFirstMediaWaitTimeoutMs()` sized.
		// A provider calls this when the first media packet arrives, so the channel stops being
		// judged by a budget meant for a source that has not sent anything yet.
		// The channel is still unpublished, so an operator-configured positive `PacketSilenceTimeoutMs`
		// governs it from here and everything else falls back to the same budget an unpublished channel is created with.
		// `PushApplication::JoinStream()` applies the effective value, `0` included, once the stream publishes.
		// This does nothing unless the wait is actually running: a media message that arrives before
		// the provider sized the wait must not consume it, and only the first call after that ends it.
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
		// A media message can reach a channel before the wait was sized,
		// so `NotStarted` and `Ended` are distinct: only the former is still waiting to be armed.
		enum class FirstMediaWaitPhase : uint8_t
		{
			NotStarted,
			Waiting,
			Ended
		};
		std::atomic<FirstMediaWaitPhase> _first_media_wait_phase = FirstMediaWaitPhase::NotStarted;
		// How many of this channel's data handlers are running
		std::atomic<int32_t> _processing_data_count				 = 0;

		// Push Provider
		std::shared_ptr<PushProvider>	_provider;
	};
}
