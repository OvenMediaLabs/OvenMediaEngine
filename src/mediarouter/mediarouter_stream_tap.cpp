//==============================================================================
//
//  MediaRouterStreamTap
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mediarouter_stream_tap.h"

std::shared_ptr<MediaRouterStreamTap> MediaRouterStreamTap::Create(size_t threshold)
{
    return std::make_shared<MediaRouterStreamTap>(threshold);
}

MediaRouterStreamTap::MediaRouterStreamTap(size_t threshold)
    : _buffer("MediaRouterStreamTap", threshold),
      _backfill_buffer("MediaRouterStreamTapBackfill", 0)
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
    _backfill_buffer.Start();
}

void MediaRouterStreamTap::Stop()
{
    _is_started = false;
	_buffer.Stop();
    _buffer.Clear();
    _backfill_buffer.Stop();
    _backfill_buffer.Clear();
}

std::shared_ptr<MediaPacket> MediaRouterStreamTap::Pop(int timeout_in_msec)
{
    if (_state != State::Tapped && _buffer.IsEmpty() && _backfill_buffer.IsEmpty())
    {
        return nullptr;
    }

    // Backfill first; it is always older than any live packet, and live packets
    // only start arriving after the whole backfill has been enqueued
    auto backfill = _backfill_buffer.Dequeue(0);
    if (backfill.has_value())
    {
        return backfill.value();
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
    return PushTo(_buffer, media_packet);
}

bool MediaRouterStreamTap::PushBackfill(const std::shared_ptr<MediaPacket> &media_packet)
{
    return PushTo(_backfill_buffer, media_packet);
}

bool MediaRouterStreamTap::PushTo(ov::Queue<std::shared_ptr<MediaPacket>> &buffer, const std::shared_ptr<MediaPacket> &media_packet)
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

    buffer.Enqueue(media_packet->ClonePacket());

    return true;
}

void MediaRouterStreamTap::SetState(State state)
{
    _state = state;
}
