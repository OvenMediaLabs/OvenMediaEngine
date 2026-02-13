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

class MediaRouterAlert
{
public:
	MediaRouterAlert();
	~MediaRouterAlert();

	void Init(const std::shared_ptr<info::Stream> &stream_info);

	bool Update(
		const cmn::MediaRouterStreamType type,
		const bool prepared,
		const ov::ManagedQueue<std::shared_ptr<MediaPacket>> &packets_queue,
		const std::shared_ptr<info::Stream> &stream_info,
		const std::shared_ptr<MediaTrack> &media_track,
		const std::shared_ptr<MediaPacket> &media_packet);

	bool DetectInvalidPacketDuration(const std::shared_ptr<info::Stream> &stream_info, const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<MediaPacket> &media_packet);
	bool DetectBBframes(const std::shared_ptr<info::Stream> &stream_info, const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<MediaPacket> &media_packet);
	bool DetectPTSDiscontinuity(const std::shared_ptr<info::Stream> &stream_info, const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<MediaPacket> &media_packet);

	// <TrackId, Values>
	std::map<MediaTrackId, int64_t> _last_pts;
	std::map<MediaTrackId, int64_t> _last_pts_ms;	

	uint32_t _alert_count_bframe;
};
