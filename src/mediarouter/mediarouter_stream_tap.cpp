//==============================================================================
//
//  MediaRouterStreamTap
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mediarouter_stream_tap.h"

std::shared_ptr<MediaRouterStreamTap> MediaRouterStreamTap::Create(size_t buffer_size)
{
    return std::make_shared<MediaRouterStreamTap>(buffer_size);
}

MediaRouterStreamTap::MediaRouterStreamTap(size_t buffer_size)
    : _buffer("MediaRouterStreamTap", buffer_size)
{
    _id = IssueUniqueId();
}

MediaRouterStreamTap::~MediaRouterStreamTap()
{
    Destroy();
}

uint32_t MediaRouterStreamTap::GetId() const
{
    return _id;
}

uint32_t MediaRouterStreamTap::IssueUniqueId()
{
    static std::atomic<uint32_t> last_issued(100);
	return last_issued ++;
}

MediaRouterStreamTap::State MediaRouterStreamTap::GetState() const
{
    return _state;
}

void MediaRouterStreamTap::SetNeedPastData(bool need_past_data)
{
	_need_past_data = need_past_data;
}

bool MediaRouterStreamTap::DoesNeedPastData() const
{
	return _need_past_data;
}

void MediaRouterStreamTap::Start()
{
    _is_started = true;
	_buffer.Start();
}

void MediaRouterStreamTap::Stop()
{
    _is_started = false;
	_buffer.Stop();
    _buffer.Clear();
}

std::shared_ptr<MediaPacket> MediaRouterStreamTap::Pop(int timeout_in_msec)
{
    if (_state != State::Tapped && _buffer.IsEmpty())
    {
        return nullptr;
    }

    auto object = _buffer.Dequeue(timeout_in_msec);
    if (object.has_value())
    {
        return object.value();
    }

    return nullptr;
}

std::shared_ptr<info::Stream> MediaRouterStreamTap::GetStreamInfo() const
{
    return _tapped_stream_info;
}

void MediaRouterStreamTap::SetStreamInfo(const std::shared_ptr<info::Stream> &stream_info)
{
    _tapped_stream_info = stream_info;
}

void MediaRouterStreamTap::Destroy()
{
    _is_destroy_requested = true;
}

bool MediaRouterStreamTap::Push(const std::shared_ptr<MediaPacket> &media_packet)
{
    if (_state != State::Tapped)
    {
        return false;
    }

    if (_is_started == false)
    {
        return false;
    }

	if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::OVEN_EVENT)
	{
		// Event packet doen't need to forward to tap
		return true;
	}

    _buffer.Enqueue(media_packet->ClonePacket());

    return true;
}

void MediaRouterStreamTap::SetState(State state)
{
    _state = state;
}
