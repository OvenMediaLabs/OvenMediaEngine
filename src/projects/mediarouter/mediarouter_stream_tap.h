//==============================================================================
//
//  MediaRouterStreamTap
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/queue.h>
#include <base/info/stream.h>

#include <base/mediarouter/media_buffer.h>
#include "mediarouter_application.h"

class MediaRouterStreamTap
{
friend class MediaRouteApplication;
public:
    static std::shared_ptr<MediaRouterStreamTap> Create(size_t buffer_size = 300);

    MediaRouterStreamTap(size_t buffer_size);
    ~MediaRouterStreamTap();

    enum class State : int8_t
    {
        Idle = 0,
        Tapped,
        UnTapped, // Set by MediiaRouter
        Error
    };

    uint32_t GetId() const;

    State GetState() const;

    void Start();
    void Stop();

	void SetNeedPastData(bool need_past_data);
	bool DoesNeedPastData() const;

    // If the stream is Tapped, MediaPacket will be popped from the buffer.
    // If the stream is not Tapped and the buffer is empty, nullptr will be returned immediately without waiting.
    std::shared_ptr<MediaPacket> Pop(int timeout_in_msec = 0);

    // return stream info reference
    std::shared_ptr<info::Stream> GetStreamInfo() const;

    void Destroy();

private:
    bool Push(const std::shared_ptr<MediaPacket> &media_packet);
    void SetStreamInfo(const std::shared_ptr<info::Stream> &stream_info);
    void SetState(State state);

    uint32_t IssueUniqueId();

    std::shared_ptr<info::Stream> _tapped_stream_info;
    ov::Queue<std::shared_ptr<MediaPacket>> _buffer;
    State _state = State::Idle;

    bool _is_destroy_requested = false;

    bool _is_started = false;

	bool _need_past_data = false;

    uint32_t _id = 0;
};