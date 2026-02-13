//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
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
#include "mediarouter_nomalize.h"
#include "mediarouter_stats.h"
#include "mediarouter_event_generator.h"
#include "mediarouter_alert.h"
#include "modules/managed_queue/managed_queue.h"

static constexpr int64_t MEDIA_ROUTE_STREAM_MAX_MIRROR_BUFFER_SIZE_MS = 2000; // Maximum size of mirror buffer

class MediaRouteStream : public MediaRouterNormalize, public MediaRouterStats, public MediaRouterEventGenerator, public MediaRouterAlert
{
public:
	MediaRouteStream(const std::shared_ptr<info::Stream> &stream, cmn::MediaRouterStreamType type);
	~MediaRouteStream();

	// Inout Stream Type
	void SetType(cmn::MediaRouterStreamType type);
	bool IsInbound() { return _type == cmn::MediaRouterStreamType::INBOUND; }
	bool IsOutbound() { return _type == cmn::MediaRouterStreamType::OUTBOUND; }

	// Queue interfaces
	void Push(const std::shared_ptr<MediaPacket> &media_packet);
	std::shared_ptr<MediaPacket> PopAndNormalize();
	
	// Return mirror buffer reference
	struct MirrorBufferItem
	{
		MirrorBufferItem(std::shared_ptr<MediaPacket> &packet)
			: packet(packet)
		{
			created_time = std::chrono::system_clock::now();
		}

		// Elapsed milliseconds
		int64_t GetElapsedMilliseconds()
		{
			auto now = std::chrono::system_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - created_time);
			return elapsed.count();
		}

		// timepoint created
		std::chrono::time_point<std::chrono::system_clock> created_time;
		std::shared_ptr<MediaPacket> packet;
	};
	std::vector<std::shared_ptr<MirrorBufferItem>> GetMirrorBuffer();

	// Query original stream information
	std::shared_ptr<info::Stream> GetStream();

	void OnStreamPrepared(bool completed);
	bool IsStreamPrepared();
	bool IsStreamReady();

	void Flush();
	
private:
	void DropNonDecodingPackets();

	bool _is_stream_prepared = false;
	bool _is_all_tracks_parsed = false;

	// Incoming/Outgoing Stream
	cmn::MediaRouterStreamType _type;

	// Stream Information
	std::shared_ptr<info::Stream> _stream = nullptr;

	// Temporary packet store. for calculating packet duration
	std::map<MediaTrackId, std::shared_ptr<MediaPacket>> _media_packet_stash;

	// Packets queue
	ov::ManagedQueue<std::shared_ptr<MediaPacket>> _packets_queue;

	// Mirror buffer
	std::vector<std::shared_ptr<MirrorBufferItem>> _mirror_buffer;
};
