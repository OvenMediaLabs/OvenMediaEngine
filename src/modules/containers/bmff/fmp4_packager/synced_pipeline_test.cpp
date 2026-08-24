//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/modules/data_format/cue_event/cue_event.h>

#include "fmp4_test_fixtures.h"
#include "synced_boundary_policy.h"

// Synced segmentation with the real packagers: the reference (video) track cuts
// at its keyframes nearest the segment cadence and publishes every realized
// boundary; the synced (audio) track cuts at those boundaries and adopts their
// numbering. The same segment number must mean the same time slot on both
// tracks whatever the delivery skew or start offset.

namespace
{
	constexpr double kSegmentDurationMs = 4000.0;
	constexpr double kChunkDurationMs = 400.0;
	constexpr int kGopFrames = 30;	// 1 s at 30 fps
	constexpr double kAudioFrameMs = 64.0 / 3.0;
	constexpr double kVideoFrameMs = 100.0 / 3.0;

	using fmp4_test::TrackPipeline;

	struct SyncedRig
	{
		std::shared_ptr<bmff::ReferenceBoundaryPolicy> reference_policy;
		TrackPipeline video;  // reference
		TrackPipeline audio;  // synced
	};

	SyncedRig MakeRig(LLHlsCueOutCutMode cue_out_cut_mode = LLHlsCueOutCutMode::Keyframe)
	{
		SyncedRig rig;

		auto observer = std::make_shared<fmp4_test::NullStorageObserver>();

		bmff::SegmentBoundaryPolicy::Config policy_config;
		policy_config.segment_duration_ms = static_cast<uint64_t>(kSegmentDurationMs);
		policy_config.chunk_duration_ms = kChunkDurationMs;
		policy_config.cue_out_cut_mode = cue_out_cut_mode;

		auto video_track = fmp4_test::MakeVideoTrack(0, 30.0, kGopFrames);
		auto reference_config = policy_config;
		reference_config.log_context = "synced_pipeline_test/video";
		auto reference_policy = std::make_shared<bmff::ReferenceBoundaryPolicy>(reference_config, 30.0);
		rig.reference_policy = reference_policy;
		rig.video = fmp4_test::MakePipeline(video_track, observer, reference_policy, static_cast<uint64_t>(kSegmentDurationMs), kChunkDurationMs, "synced_pipeline_test");

		auto audio_track = fmp4_test::MakeAudioTrack(1);
		auto synced_config = policy_config;
		synced_config.log_context = "synced_pipeline_test/audio";
		auto synced_policy = std::make_shared<bmff::SyncedBoundaryPolicy>(reference_policy, synced_config);
		rig.audio = fmp4_test::MakePipeline(audio_track, observer, synced_policy, static_cast<uint64_t>(kSegmentDurationMs), kChunkDurationMs, "synced_pipeline_test");

		return rig;
	}

	// The synced-mode marker protocol: only the reference accepts markers, the
	// synced tracks receive them relayed on the realized boundaries
	bool InsertCueMarkerToReference(SyncedRig &rig, CueEvent::CueType type, int64_t timestamp_ms, uint32_t duration_ms, bool provisional = false)
	{
		auto data = CueEvent::Create(type, duration_ms, 0, provisional)->Serialize();
		auto marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, timestamp_ms, timestamp_ms, data);
		if (marker == nullptr)
		{
			return false;
		}

		EXPECT_FALSE(rig.audio.policy->AcceptsMarkers());

		return rig.reference_policy->InsertMarker(marker);
	}

	// Drive both pipelines interleaved by delivery time; audio_dts_lead_ms > 0
	// delivers audio that far ahead of video (the skew the chunk gate absorbs)
	void DriveInterleaved(SyncedRig &rig, int64_t total_duration_ms,
						  int64_t video_start_ms = 0, int64_t audio_start_ms = 0, int64_t audio_dts_lead_ms = 0,
						  const std::vector<int64_t> &cue_out_timestamps = {}, uint32_t cue_duration_ms = 0)
	{
		int64_t video_index = 0;
		int64_t audio_index = 0;
		size_t cue_index = 0;

		while (true)
		{
			const int64_t video_dts = video_start_ms + video_index * 100 / 3;
			const int64_t audio_dts = audio_start_ms + audio_index * 64 / 3;
			const int64_t now_ms = std::min(video_dts, audio_dts - audio_dts_lead_ms);

			if (now_ms >= total_duration_ms)
			{
				break;
			}

			if (cue_index < cue_out_timestamps.size() && now_ms >= cue_out_timestamps[cue_index] - 1000)
			{
				ASSERT_TRUE(InsertCueMarkerToReference(rig, CueEvent::CueType::OUT, cue_out_timestamps[cue_index], cue_duration_ms));
				ASSERT_TRUE(InsertCueMarkerToReference(rig, CueEvent::CueType::IN, cue_out_timestamps[cue_index] + cue_duration_ms, 0, true));
				cue_index++;
			}

			if (video_dts <= audio_dts - audio_dts_lead_ms)
			{
				const int64_t duration = video_start_ms + (video_index + 1) * 100 / 3 - video_dts;
				ASSERT_TRUE(rig.video.packager->AppendSample(fmp4_test::MakeVideoFrame(0, video_dts, duration, (video_index % kGopFrames) == 0)));
				video_index++;
			}
			else
			{
				const int64_t duration = audio_start_ms + (audio_index + 1) * 64 / 3 - audio_dts;
				ASSERT_TRUE(rig.audio.packager->AppendSample(fmp4_test::MakeAudioFrame(1, audio_dts, duration)));
				audio_index++;
			}
		}

		ASSERT_EQ(cue_index, cue_out_timestamps.size());
	}

	// Every segment number present and completed on both tracks must mean the
	// same time slot; audio quantizes to its frame size. The first audio
	// segment of a late-started track begins mid-slot, so only its end counts.
	void ExpectAlignedSegments(const SyncedRig &rig, size_t min_common_segments)
	{
		size_t common_segments = 0;
		bool audio_first_seen = false;

		for (int64_t number = 0; number <= rig.video.storage->GetLastSegmentNumber(); number++)
		{
			auto video_segment = rig.video.storage->GetSegment(number);
			auto audio_segment = rig.audio.storage->GetSegment(number);

			bool is_first_audio_segment = (audio_segment != nullptr && audio_first_seen == false);
			if (audio_segment != nullptr)
			{
				audio_first_seen = true;
			}

			if (video_segment == nullptr || audio_segment == nullptr ||
				video_segment->IsCompleted() == false || audio_segment->IsCompleted() == false)
			{
				continue;
			}

			common_segments++;

			double video_start_ms = static_cast<double>(video_segment->GetStartTimestamp());
			double audio_start_ms = static_cast<double>(audio_segment->GetStartTimestamp());
			double video_end_ms = video_start_ms + video_segment->GetDurationMs();
			double audio_end_ms = audio_start_ms + audio_segment->GetDurationMs();

			if (is_first_audio_segment == false)
			{
				EXPECT_NEAR(video_start_ms, audio_start_ms, kAudioFrameMs + 1.0) << "segment " << number;
			}

			EXPECT_NEAR(video_end_ms, audio_end_ms, kAudioFrameMs + 1.0) << "segment " << number;
		}

		EXPECT_GE(common_segments, min_common_segments);
	}
}  // namespace

TEST(SyncedPipelineTest, SegmentsAlignFromTheStart)
{
	auto rig = MakeRig();
	DriveInterleaved(rig, 60000);

	// Keyframes sit on the segment cadence, so the reference cuts exactly at
	// multiples of the duration and the audio follows within one frame
	ExpectAlignedSegments(rig, 12);
}

TEST(SyncedPipelineTest, AudioDeliveredAheadStaysAligned)
{
	auto rig = MakeRig();
	DriveInterleaved(rig, 60000, 0, 0, 1000);

	ExpectAlignedSegments(rig, 12);
}

struct MarkerObservation
{
	int64_t segment_number = -1;
	std::shared_ptr<Marker> marker;
	double segment_end_ms = 0.0;
};

static std::map<size_t, MarkerObservation> CollectMarkers(const TrackPipeline &pipeline, CueEvent::CueType type)
{
	std::map<size_t, MarkerObservation> observations;
	size_t index = 0;

	for (int64_t number = 0; number <= pipeline.storage->GetLastSegmentNumber(); number++)
	{
		auto segment = std::static_pointer_cast<bmff::FMP4Segment>(pipeline.storage->GetSegment(number));
		if (segment == nullptr || segment->HasMarker() == false)
		{
			continue;
		}

		for (const auto &marker : segment->GetMarkers())
		{
			auto cue_event = marker->GetCueEvent();
			if (cue_event == nullptr || cue_event->GetCueType() != type)
			{
				continue;
			}

			observations[index++] = {segment->GetNumber(), marker,
									 static_cast<double>(segment->GetStartTimestamp()) + segment->GetDurationMs()};
		}
	}

	return observations;
}

// The marker protocol on the realized boundaries, in both cut modes: the OUT
// cuts where its mode dictates, the IN (a return point) cuts at the first
// keyframe at or after it, and every synced track carries the same marker on
// the same segment number.
static void ExpectMarkersRideTheBoundary(LLHlsCueOutCutMode cut_mode)
{
	auto rig = MakeRig(cut_mode);

	// Both cues sit mid-GOP (keyframes are at multiples of 1000)
	std::vector<int64_t> cue_out_timestamps = {13210, 33210};
	DriveInterleaved(rig, 60000, 0, 0, 0, cue_out_timestamps, 10000);

	for (auto type : {CueEvent::CueType::OUT, CueEvent::CueType::IN})
	{
		auto video_markers = CollectMarkers(rig.video, type);
		auto audio_markers = CollectMarkers(rig.audio, type);

		ASSERT_EQ(video_markers.size(), cue_out_timestamps.size());
		ASSERT_EQ(audio_markers.size(), cue_out_timestamps.size());

		for (size_t index = 0; index < cue_out_timestamps.size(); index++)
		{
			// One marker object, on the same segment number, on both tracks
			EXPECT_EQ(video_markers[index].segment_number, audio_markers[index].segment_number) << "cue " << index;
			EXPECT_EQ(video_markers[index].marker, audio_markers[index].marker) << "cue " << index;

			double requested_ms = static_cast<double>((type == CueEvent::CueType::OUT) ? cue_out_timestamps[index] : cue_out_timestamps[index] + 10000);
			double realized_ms = video_markers[index].segment_end_ms;

			if (type == CueEvent::CueType::OUT && cut_mode == LLHlsCueOutCutMode::Immediate)
			{
				// An immediate OUT cuts at the first sample boundary at or after
				// its position, mid-GOP included (samples are not splittable)
				EXPECT_GE(realized_ms, requested_ms) << "cue " << index;
				EXPECT_LT(realized_ms, requested_ms + kVideoFrameMs + 1.0) << "cue " << index;
			}
			else
			{
				// A keyframe cut waits for the first keyframe at or after the position
				double next_keyframe_ms = std::ceil(requested_ms / 1000.0) * 1000.0;
				EXPECT_NEAR(realized_ms, next_keyframe_ms, 0.1) << "cue " << index;
			}

			// The audio cut lands on the same boundary within one audio frame
			EXPECT_GE(audio_markers[index].segment_end_ms, realized_ms - 0.1) << "cue " << index;
			EXPECT_LT(audio_markers[index].segment_end_ms, realized_ms + kAudioFrameMs * 2.0 + 1.0) << "cue " << index;
		}
	}
}

TEST(SyncedPipelineTest, MarkersRideTheRealizedBoundary)
{
	ExpectMarkersRideTheBoundary(LLHlsCueOutCutMode::Keyframe);
}

TEST(SyncedPipelineTest, ImmediateCutRealizesTheMarkerMidGop)
{
	ExpectMarkersRideTheBoundary(LLHlsCueOutCutMode::Immediate);
}

TEST(SyncedPipelineTest, LateStartingAudioAdoptsTheSlotNumbering)
{
	auto rig = MakeRig();

	// Video from 0, audio joins mid-slot at 4200: the audio playlist simply has
	// no segment 0 and the numbering agrees from segment 1 on
	DriveInterleaved(rig, 40000, 0, 4200);

	EXPECT_EQ(rig.audio.storage->GetSegment(0), nullptr);

	auto first_audio_segment = rig.audio.storage->GetSegment(1);
	ASSERT_NE(first_audio_segment, nullptr);
	EXPECT_NEAR(static_cast<double>(first_audio_segment->GetStartTimestamp()), 4200.0, kAudioFrameMs + 1.0);

	ExpectAlignedSegments(rig, 7);
}
