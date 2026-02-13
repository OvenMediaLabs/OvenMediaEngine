//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>

#include <memory>
#include <queue>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"
#include "base/mediarouter/mediarouter_application_connector.h"
#include "modules/managed_queue/managed_queue.h"

class MediaRouterStats
{
public:
	MediaRouterStats();
	~MediaRouterStats();

	void Init(const std::shared_ptr<info::Stream> &stream_info);

	void Update(
		const int8_t type,
		const bool prepared,
		const ov::ManagedQueue<std::shared_ptr<MediaPacket>> &packets_queue,
		const std::shared_ptr<info::Stream> &stream_info,
		const std::shared_ptr<MediaTrack> &media_track,
		const std::shared_ptr<MediaPacket> &media_packet);

	// <TrackId, Values>
	std::map<MediaTrackId, int64_t> _stat_recv_pkt_lpts;
	std::map<MediaTrackId, int64_t> _stat_recv_pkt_ldts;
	std::map<MediaTrackId, int64_t> _stat_recv_pkt_adur;

	// Time for statistics
	ov::StopWatch _stop_watch;
	std::chrono::time_point<std::chrono::system_clock> _last_recv_time;
	std::chrono::time_point<std::chrono::system_clock> _stat_start_time;
};
