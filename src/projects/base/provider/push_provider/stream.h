//==============================================================================
//
//  PushProvider Stream Base Class 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/ovlibrary/stop_watch.h>
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

		// channel may not yet determined, so we manage the timer separately
		void UpdateLastReceivedTime();
		void SetPacketSilenceTimeoutMs(time_t timeout_ms);
		time_t GetPacketSilenceTimeoutMs();
		time_t GetElapsedMsSinceLastReceived();
		
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
		ov::StopWatch 	_packet_silence_timer;
		time_t			_packet_silence_timeout_ms = 0;

		uint32_t		_attemps_publish_count = 0;

		// Push Provider
		std::shared_ptr<PushProvider>	_provider;
	};
}
