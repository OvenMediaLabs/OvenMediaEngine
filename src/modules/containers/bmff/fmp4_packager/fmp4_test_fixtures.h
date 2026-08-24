//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>

#include "fmp4_packager.h"
#include "fmp4_storage.h"

// The pieces every fmp4 packager test assembles: a storage observer that
// records nothing, a per-track pipeline (policy, storage, packager), and the
// media packets to drive it.

namespace fmp4_test
{
	class NullStorageObserver : public bmff::FMp4StorageObserver
	{
	public:
		void OnFMp4StorageInitialized(const int32_t &track_id) override {}
		void OnMediaSegmentCreated(const int32_t &track_id, const uint32_t &segment_number) override {}
		void OnMediaChunkUpdated(const int32_t &track_id, const uint32_t &segment_number, const uint32_t &chunk_number, bool last_chunk) override {}
		void OnMediaSegmentDeleted(const int32_t &track_id, const uint32_t &segment_number) override {}
		void OnMediaSegmentCompleted(const int32_t &track_id, const uint32_t &segment_number) override {}
	};

	struct TrackPipeline
	{
		std::shared_ptr<MediaTrack> track;
		std::shared_ptr<bmff::SegmentBoundaryPolicy> policy;
		std::shared_ptr<bmff::FMP4Storage> storage;
		std::shared_ptr<bmff::FMP4Packager> packager;
	};

	// timescale 1000 unless a test needs the rounding of a real media timebase
	inline std::shared_ptr<MediaTrack> MakeVideoTrack(uint32_t track_id, double frame_rate, int32_t gop_frames, int32_t timescale = 1000)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(track_id);
		track->SetMediaType(cmn::MediaType::Video);
		track->SetCodecId(cmn::MediaCodecId::H264);
		track->SetTimeBase(1, timescale);
		track->SetFrameRateByConfig(frame_rate);
		track->SetKeyFrameIntervalByConfig(gop_frames);
		return track;
	}

	inline std::shared_ptr<MediaTrack> MakeAudioTrack(uint32_t track_id, int32_t timescale = 1000)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(track_id);
		track->SetMediaType(cmn::MediaType::Audio);
		track->SetCodecId(cmn::MediaCodecId::Aac);
		track->SetTimeBase(1, timescale);
		return track;
	}

	inline TrackPipeline MakePipeline(const std::shared_ptr<MediaTrack> &track,
									  const std::shared_ptr<bmff::FMp4StorageObserver> &observer,
									  const std::shared_ptr<bmff::SegmentBoundaryPolicy> &boundary_policy,
									  uint64_t segment_duration_ms,
									  double chunk_duration_ms,
									  const ov::String &tag)
	{
		bmff::FMP4Storage::Config storage_config;
		storage_config.max_segments = 100000;  // keep everything for inspection
		storage_config.segment_duration_ms = segment_duration_ms;

		bmff::FMP4Packager::Config packager_config;
		packager_config.chunk_duration_ms = chunk_duration_ms;

		TrackPipeline pipeline;
		pipeline.track = track;
		pipeline.policy = boundary_policy;
		pipeline.storage = std::make_shared<bmff::FMP4Storage>(observer, track, storage_config, tag, boundary_policy);
		pipeline.packager = std::make_shared<bmff::FMP4Packager>(pipeline.storage, boundary_policy, track, nullptr, packager_config);
		return pipeline;
	}

	inline std::shared_ptr<MediaPacket> MakeVideoFrame(uint32_t track_id, int64_t dts, int64_t duration, bool keyframe, int64_t pts = -1)
	{
		auto data = std::make_shared<ov::Data>();
		uint8_t byte = 0x00;
		data->Append(&byte, 1);
		return std::make_shared<MediaPacket>(cmn::MediaType::Video, track_id, data, (pts >= 0) ? pts : dts, dts, duration,
											 keyframe ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
											 cmn::BitstreamFormat::H264_AVCC, cmn::PacketType::NALU);
	}

	inline std::shared_ptr<MediaPacket> MakeAudioFrame(uint32_t track_id, int64_t dts, int64_t duration)
	{
		auto data = std::make_shared<ov::Data>();
		uint8_t byte = 0x00;
		data->Append(&byte, 1);
		return std::make_shared<MediaPacket>(cmn::MediaType::Audio, track_id, data, dts, dts, duration,
											 MediaPacketFlag::Key, cmn::BitstreamFormat::AAC_RAW, cmn::PacketType::RAW);
	}

	// The completed segment whose range covers the given position
	inline std::shared_ptr<bmff::FMP4Segment> FindSegmentCovering(const TrackPipeline &pipeline, double position_ms)
	{
		for (int64_t number = 0; number <= pipeline.storage->GetLastSegmentNumber(); number++)
		{
			auto segment = std::static_pointer_cast<bmff::FMP4Segment>(pipeline.storage->GetSegment(number));
			if (segment == nullptr || segment->IsCompleted() == false)
			{
				continue;
			}

			double start_ms = static_cast<double>(segment->GetStartTimestamp());
			double end_ms = start_ms + segment->GetDurationMs();
			if (start_ms <= position_ms && position_ms <= end_ms)
			{
				return segment;
			}
		}

		return nullptr;
	}
}  // namespace fmp4_test
