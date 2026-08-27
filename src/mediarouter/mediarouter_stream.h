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

#include <chrono>
#include <map>
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

// Mirror buffer retention window, measured in media time (DTS). A video track
// keeps only its current GOP and is cleared whole when the GOP grows longer
// than this; non-video tracks keep this much trailing content
static constexpr int64_t MEDIA_ROUTE_STREAM_MIRROR_RETENTION_MS = 10000;

// Backfill depth for non-video tracks when there is no video track to align with
static constexpr int64_t MEDIA_ROUTE_STREAM_MIRROR_BASE_BACKFILL_MS = 2000;

// Warn if a track stays invalid (no decodable media) this long after the first packet, which blocks stream prepare
static constexpr int64_t MEDIA_ROUTE_STREAM_TRACK_PREPARE_TIMEOUT_MS = 3000;

class MediaRouteStream : public MediaRouterNormalize, public MediaRouterStats, public MediaRouterEventGenerator, public MediaRouterAlert
{
public:
	MediaRouteStream(const std::shared_ptr<info::Stream> &stream, cmn::MediaRouterStreamType type, uint32_t worker_id);
	~MediaRouteStream();

	// Inout Stream Type
	void SetType(cmn::MediaRouterStreamType type);
	void SetBufferRetentionDuration(int delay_ms);
	bool IsInbound() { return _type == cmn::MediaRouterStreamType::INBOUND; }
	bool IsOutbound() { return _type == cmn::MediaRouterStreamType::OUTBOUND; }

	// Queue interfaces
	void Push(const std::shared_ptr<MediaPacket> &media_packet);
	std::shared_ptr<MediaPacket> PopAndNormalize();
	
	struct MirrorBufferItem
	{
		MirrorBufferItem(std::shared_ptr<MediaPacket> &packet, int64_t dts_us)
			: dts_us(dts_us), packet(packet)
		{
			created_time = std::chrono::steady_clock::now();
		}

		// Elapsed milliseconds
		int64_t GetElapsedMilliseconds()
		{
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - created_time);
			return elapsed.count();
		}

		// timepoint created
		std::chrono::time_point<std::chrono::steady_clock> created_time;

		// DTS converted to microseconds; monotonic per track and comparable
		// across tracks of the stream
		int64_t dts_us = 0;

		std::shared_ptr<MediaPacket> packet;
	};

	// Per-track mirror buffers, each in arrival order
	using MirrorBufferMap = std::map<MediaTrackId, std::vector<std::shared_ptr<MirrorBufferItem>>>;

	MirrorBufferMap GetMirrorBuffers();

	// Applies the retention policy when a packet enters its track's mirror buffer.
	// A video track holds only its current GOP: a keyframe restarts the buffer and
	// a GOP longer than the retention window is discarded whole, so a video track
	// is always keyframe-led or empty. Non-video keeps a sliding content window
	static void RetainMirrorBuffer(MirrorBufferMap &mirror_buffers, std::shared_ptr<MediaPacket> &media_packet, int64_t dts_us);

	// Builds the past-data backfill for a new tap: fresh video tracks are included
	// whole from their leading keyframe, and non-video is cut at the earliest
	// included keyframe's DTS so the backfill starts aligned
	static std::vector<std::shared_ptr<MirrorBufferItem>> BuildPastData(const MirrorBufferMap &mirror_buffers);

	// Query original stream information
	std::shared_ptr<info::Stream> GetStream();

	// MediaRouter worker assigned round-robin at construction, immutable for the stream's life.
	uint32_t GetWorkerID() const { return _worker_id; }

	void OnStreamPrepared(bool completed);
	bool IsStreamPrepared();
	bool IsStreamReady();

	// The prepared notification must run exactly once even when two threads
	// pass the readiness check at the same time (creation thread + worker)
	bool MarkPreparedNotified();

	// Periodically warn about tracks that stay invalid too long, which blocks the stream from being prepared
	void CheckUnpreparedTrackTimeout();

	void Flush();
	
private:
	// Author state per track, accessed only on the worker thread of this stream.
	// `working` is the private mutable copy the normalizer parses into; a new
	// immutable version is cloned from it and published on a content change.
	// `last_hint` makes an upstream hint adopted exactly once per hint object;
	// `last_dcr` re-arms the rebuild trigger after a content-equal replacement.
	struct TrackAuthorState
	{
		std::shared_ptr<MediaTrack> working = nullptr;
		std::shared_ptr<const MediaTrack> published = nullptr;
		std::shared_ptr<const MediaTrack> last_hint = nullptr;
		std::shared_ptr<DecoderConfigurationRecord> last_dcr = nullptr;
		bool recheck = false;
	};

	void DropNonDecodingPackets();

	// Publish a new immutable track version from the author state when the
	// content changed, and attach the current version to the packet. This
	// stream is the single author of track versions for its direction.
	void StampTrack(TrackAuthorState &state, const std::shared_ptr<MediaPacket> &media_packet);

	// Apply an in-band command to the author state (e.g. UpdateSubtitleLanguage)
	void HandleEventPacket(const std::shared_ptr<MediaPacket> &media_packet);

	// Publish an immutable version from the author state when it differs from
	// the published one (content or labels)
	void PublishWorkingVersion(TrackAuthorState &state, uint32_t track_id);

	// Adopt a provider/upstream-authored track version carried by the packet
	// into the author state, so extradata-dependent formats stay decodable
	// without cross-module track mutation. Runs before normalization.
	void ApplyPacketConfigHint(TrackAuthorState &state, const std::shared_ptr<MediaPacket> &media_packet);

	bool _is_stream_prepared = false;
	std::atomic<bool> _prepared_notified = false;
	bool _is_all_tracks_parsed = false;

	// Deadline tracking for periodically warning about tracks that never become valid
	std::chrono::steady_clock::time_point _last_unprepared_warn_time;

	// Incoming/Outgoing Stream
	cmn::MediaRouterStreamType _type;

	const uint32_t _worker_id;

	// Stream Information
	std::shared_ptr<info::Stream> _stream = nullptr;

	// Held next to the stream so the per-packet path does not copy the pointer
	std::shared_ptr<info::StreamStats> _stats = nullptr;

	std::map<MediaTrackId, TrackAuthorState> _track_authors;

	// Temporary packet store. for calculating packet duration
	std::map<MediaTrackId, std::shared_ptr<MediaPacket>> _media_packet_stash;

	// Packets queue
	ov::ManagedQueue<std::shared_ptr<MediaPacket>> _packets_queue;

	// Mirror buffers, one per track
	MirrorBufferMap _mirror_buffers;
};
