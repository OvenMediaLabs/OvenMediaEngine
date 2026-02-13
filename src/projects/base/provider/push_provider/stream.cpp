//==============================================================================
//
//  PushStream
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================


#include "provider.h"
#include "application.h"
#include "stream.h"
#include "provider_private.h"

namespace pvd
{
	PushStream::PushStream(StreamSourceType source_type, ov::String channel_name, uint32_t channel_id, const std::shared_ptr<PushProvider> &provider)
		: PushStream(source_type, channel_id, provider)
	{
		SetName(channel_name);
	}

	PushStream::PushStream(StreamSourceType source_type, uint32_t channel_id, const std::shared_ptr<PushProvider> &provider)
		: Stream(source_type)
	{
		SetId(pvd::Application::IssueUniqueStreamId());
		_channel_id = channel_id;
		_provider = provider;
	}

	PushStream::PushStream(StreamSourceType source_type, ov::String channel_name, const std::shared_ptr<PushProvider> &provider)
		: PushStream(source_type, provider)
	{
		SetName(channel_name);
	}

	PushStream::PushStream(StreamSourceType source_type, const std::shared_ptr<PushProvider> &provider)
		: Stream(source_type)
	{
		auto id = pvd::Application::IssueUniqueStreamId();
		SetId(id);
		// If the channel id is not given, use the stream id as the channel id
		_channel_id = id;
		_provider = provider;
	}

	bool PushStream::Terminate()
	{
		// To PushStream, Terminate has the same meaning as Stop.
		if (Stop() == false)
		{
			return false;
		}

		return Stream::Terminate();
	}

	uint32_t PushStream::GetChannelId()
	{
		return _channel_id;
	}

	uint32_t PushStream::GetRelatedChannelId()
	{
		return _related_channel_id;
	}

	void PushStream::SetRelatedChannelId(uint32_t related_channel_id)
	{
		_related_channel_id = related_channel_id;
	}

	void PushStream::UpdateLastReceivedTime()
	{
		_packet_silence_timer.Update();
	}

	void PushStream::SetPacketSilenceTimeoutMs(time_t timeout_ms)
	{
		_packet_silence_timeout_ms = timeout_ms;
	}
	
	time_t PushStream::GetPacketSilenceTimeoutMs()
	{
		return _packet_silence_timeout_ms;
	}

	time_t PushStream::GetElapsedMsSinceLastReceived()
	{
		return _packet_silence_timer.Elapsed();
	}

	bool PushStream::PublishChannel(const info::VHostAppName &vhost_app_name)
	{
		if(GetProvider() == nullptr)
		{
			return false;
		}

		_attemps_publish_count++;
		
		_is_published = GetProvider()->PublishChannel(GetChannelId(), vhost_app_name, GetSharedPtrAs<PushStream>());

		_packet_silence_timer.Start();

		return _is_published;
	}

	bool PushStream::DoesBelongApplication()
	{
		return GetApplication() != nullptr;
	}

	bool PushStream::IsPublished()
	{
		return _is_published;
	}

	bool PushStream::IsReadyToReceiveStreamData()
	{
		// Check if it is signalling channel
		if(GetPushStreamType() == PushStreamType::SIGNALLING || GetPushStreamType() == PushStreamType::UNKNOWN)
		{
			return false;
		}

		// Check if it has stream name
		if(GetName().GetLength() == 0)
		{
			return false;
		}

		// Check if it has track information
		if(GetTracks().size() == 0)
		{
			return false;
		}

		return true;
	}
}