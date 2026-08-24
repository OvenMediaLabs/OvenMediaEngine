//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/modules/data_format/cue_event/cue_event.h>

#include "duration_boundary_policy.h"
#include "fmp4_test_fixtures.h"

// Markers under the boundary policy: the policy owns the pending markers, cuts
// the segments at their positions (a CUE-OUT right there or at the next
// keyframe, per the configured mode; a CUE-IN always at the next keyframe),
// attaches them to the segments that covered them, and clamps or refuses
// positions that have already passed.

namespace
{
	constexpr uint64_t kSegmentDurationMs = 4000;
	constexpr double kChunkDurationMs = 400.0;
	constexpr int kGopFrames = 30;	// keyframes at multiples of 1000 ms at 30 fps
	constexpr uint32_t kCueDurationMs = 10000;

	bmff::SegmentBoundaryPolicy::Config MakeConfig(LLHlsCueOutCutMode cue_out_cut_mode = LLHlsCueOutCutMode::Keyframe)
	{
		bmff::SegmentBoundaryPolicy::Config config;
		config.segment_duration_ms = kSegmentDurationMs;
		config.chunk_duration_ms = kChunkDurationMs;
		config.cue_out_cut_mode = cue_out_cut_mode;
		config.log_context = "marker_test";
		return config;
	}

	using fmp4_test::TrackPipeline;

	TrackPipeline MakeVideoPipeline(LLHlsCueOutCutMode cue_out_cut_mode)
	{
		auto track = fmp4_test::MakeVideoTrack(0, 30.0, kGopFrames);
		auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(cue_out_cut_mode), false);
		return fmp4_test::MakePipeline(track, std::make_shared<fmp4_test::NullStorageObserver>(), policy,
									   kSegmentDurationMs, kChunkDurationMs, "marker_test");
	}

	// Drive 30 fps video frames through [from_ms, to_ms)
	void Drive(TrackPipeline &pipeline, int64_t from_ms, int64_t to_ms, int64_t *frame_index)
	{
		while (true)
		{
			const int64_t dts = (*frame_index) * 100 / 3;
			if (dts >= to_ms)
			{
				break;
			}
			if (dts >= from_ms)
			{
				const int64_t duration = ((*frame_index) + 1) * 100 / 3 - dts;
				ASSERT_TRUE(pipeline.packager->AppendSample(fmp4_test::MakeVideoFrame(0, dts, duration, ((*frame_index) % kGopFrames) == 0)));
			}
			(*frame_index)++;
		}
	}

	std::shared_ptr<Marker> MakeCueMarker(CueEvent::CueType type, int64_t timestamp_ms, uint32_t duration_ms, bool provisional = false)
	{
		auto data = CueEvent::Create(type, duration_ms, 0, provisional)->Serialize();
		return Marker::CreateMarker(cmn::BitstreamFormat::CUE, timestamp_ms, timestamp_ms, data);
	}

	bool InsertCue(const TrackPipeline &pipeline, CueEvent::CueType type, int64_t timestamp_ms, uint32_t duration_ms, bool provisional = false)
	{
		auto marker = MakeCueMarker(type, timestamp_ms, duration_ms, provisional);
		auto [can_insert, message] = pipeline.policy->CanInsertMarker(marker);
		if (can_insert == false)
		{
			return false;
		}
		return pipeline.policy->InsertMarker(marker);
	}

	using fmp4_test::FindSegmentCovering;
}  // namespace

//------------------------------------------------------------------------------
// Insertion rules (policy level)
//------------------------------------------------------------------------------

TEST(MarkerPolicyTest, FirstMarkerMustBeOut)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	auto in_marker = MakeCueMarker(CueEvent::CueType::IN, 5000, 0);
	EXPECT_FALSE(std::get<0>(policy->CanInsertMarker(in_marker)));

	auto out_marker = MakeCueMarker(CueEvent::CueType::OUT, 5000, kCueDurationMs);
	EXPECT_TRUE(std::get<0>(policy->CanInsertMarker(out_marker)));
}

TEST(MarkerPolicyTest, BreakMustBeLongEnoughToReturnFrom)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	// 1.5 segments is the minimum gap an IN needs after its OUT
	auto short_out = MakeCueMarker(CueEvent::CueType::OUT, 5000, 3000);
	EXPECT_FALSE(std::get<0>(policy->CanInsertMarker(short_out)));
}

TEST(MarkerPolicyTest, OutAfterOutIsRejected)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	ASSERT_TRUE(policy->InsertMarker(MakeCueMarker(CueEvent::CueType::OUT, 5000, kCueDurationMs)));
	EXPECT_FALSE(std::get<0>(policy->CanInsertMarker(MakeCueMarker(CueEvent::CueType::OUT, 9000, kCueDurationMs))));
}

TEST(MarkerPolicyTest, InPairsWithItsOut)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	auto out_marker = MakeCueMarker(CueEvent::CueType::OUT, 5000, kCueDurationMs);
	ASSERT_TRUE(policy->InsertMarker(out_marker));

	// Too close to the OUT: the break could not be packaged
	EXPECT_FALSE(std::get<0>(policy->CanInsertMarker(MakeCueMarker(CueEvent::CueType::IN, 7000, 0))));

	auto in_marker = MakeCueMarker(CueEvent::CueType::IN, 15000, 0);
	ASSERT_TRUE(policy->InsertMarker(in_marker));
	EXPECT_EQ(in_marker->GetParent(), out_marker);

	// An explicit IN may replace the pending one with an earlier return point,
	// keeping the pairing
	auto earlier_in = MakeCueMarker(CueEvent::CueType::IN, 12000, 0);
	ASSERT_TRUE(policy->InsertMarker(earlier_in));
	EXPECT_EQ(earlier_in->GetParent(), out_marker);

	// A later explicit IN may not extend the pending explicit one
	EXPECT_FALSE(std::get<0>(policy->CanInsertMarker(MakeCueMarker(CueEvent::CueType::IN, 14000, 0))));
}

TEST(MarkerPolicyTest, PreparingAMarkerChangesNothingUntilItIsCommitted)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	auto prepared = policy->PrepareMarker(MakeCueMarker(CueEvent::CueType::OUT, 5000, kCueDurationMs));
	ASSERT_TRUE(prepared.has_value());

	// The break is not open yet: its return point is still refused, and a
	// settlement covering the position carries nothing
	EXPECT_FALSE(std::get<0>(policy->CanInsertMarker(MakeCueMarker(CueEvent::CueType::IN, 15000, 0))));

	bmff::CompletedSegment completed;
	completed.number = 0;
	completed.start_timestamp_us = 4000000;
	completed.duration_us = 4000000;
	EXPECT_TRUE(policy->OnSegmentCompleted(completed).markers.empty());

	// Applying it opens the break
	policy->CommitMarker(prepared.value());
	EXPECT_TRUE(std::get<0>(policy->CanInsertMarker(MakeCueMarker(CueEvent::CueType::IN, 15000, 0))));

	bmff::CompletedSegment next;
	next.number = 1;
	next.start_timestamp_us = 4000000;
	next.duration_us = 4000000;
	auto result = policy->OnSegmentCompleted(next);
	ASSERT_EQ(result.markers.size(), 1u);
	EXPECT_EQ(result.markers.front()->GetTimestampMs(), 5000);
}

//------------------------------------------------------------------------------
// Cut positions (through the packager)
//------------------------------------------------------------------------------

TEST(MarkerPolicyTest, OutCutsImmediatelyMidGop)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Immediate);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 3000, &frame_index);
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::OUT, 4210, kCueDurationMs));
	Drive(pipeline, 3000, 8000, &frame_index);

	// The segment ends right at the marker, mid-GOP: at the first frame at or
	// after 4210, well before the next keyframe at 5000
	auto segment = FindSegmentCovering(pipeline, 4210.0);
	ASSERT_NE(segment, nullptr);
	double end_ms = static_cast<double>(segment->GetStartTimestamp()) + segment->GetDurationMs();
	EXPECT_NEAR(end_ms, 4210.0, 100.0 / 3.0 + 1.0);

	// The closing segment carries the marker
	ASSERT_TRUE(segment->HasMarker());
	EXPECT_EQ(segment->GetMarkers().front()->GetTimestampMs(), 4210);
}

TEST(MarkerPolicyTest, OutCutsAtKeyframeWhenConfigured)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Keyframe);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 3000, &frame_index);
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::OUT, 4210, kCueDurationMs));
	Drive(pipeline, 3000, 8000, &frame_index);

	// Client-side ad insertion: the original keeps playing, so the cut waits
	// for the first keyframe at or after the marker
	auto segment = FindSegmentCovering(pipeline, 4210.0);
	ASSERT_NE(segment, nullptr);
	double end_ms = static_cast<double>(segment->GetStartTimestamp()) + segment->GetDurationMs();
	EXPECT_NEAR(end_ms, 5000.0, 1.0);
	EXPECT_TRUE(segment->HasMarker());
}

TEST(MarkerPolicyTest, InCutsAtKeyframe)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Immediate);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 3000, &frame_index);
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::OUT, 4210, kCueDurationMs));
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::IN, 4210 + kCueDurationMs, 0, true));
	Drive(pipeline, 3000, 20000, &frame_index);

	// The return point cuts at the first keyframe at or after it
	auto segment = FindSegmentCovering(pipeline, 4210.0 + kCueDurationMs);
	ASSERT_NE(segment, nullptr);
	double end_ms = static_cast<double>(segment->GetStartTimestamp()) + segment->GetDurationMs();
	EXPECT_NEAR(end_ms, 15000.0, 1.0);
	ASSERT_TRUE(segment->HasMarker());

	// The IN rides the segment paired with its OUT
	auto in_marker = segment->GetMarkers().back();
	ASSERT_NE(in_marker->GetParent(), nullptr);
	EXPECT_EQ(in_marker->GetParent()->GetTimestampMs(), 4210);
}

TEST(MarkerPolicyTest, MarkerSegmentIsDiscountedFromThePacing)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Immediate);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 3000, &frame_index);
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::OUT, 4210, kCueDurationMs));
	Drive(pipeline, 3000, 10000, &frame_index);

	// The marker cut ended a segment early; the next one fills only up to the
	// original cadence position, so the track returns to the 4-second grid
	auto segment = FindSegmentCovering(pipeline, 6000.0);
	ASSERT_NE(segment, nullptr);
	double end_ms = static_cast<double>(segment->GetStartTimestamp()) + segment->GetDurationMs();
	EXPECT_NEAR(end_ms, 8000.0, 100.0 / 3.0 + 1.0);
}

TEST(MarkerPolicyTest, MarkerInsideAKeyframeSpanDoesNotCutEarly)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Keyframe);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 4500, &frame_index);

	// 5010 ms falls inside the keyframe's own span [5000, 5033): the cut waits
	// for the next keyframe so the closing segment covers the position; the
	// keyframe at 5000 must not close a segment that ends before the marker
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::OUT, 5010, kCueDurationMs));
	Drive(pipeline, 4500, 9000, &frame_index);

	auto segment = FindSegmentCovering(pipeline, 4500.0);
	ASSERT_NE(segment, nullptr);
	double end_ms = static_cast<double>(segment->GetStartTimestamp()) + segment->GetDurationMs();
	EXPECT_NEAR(end_ms, 6000.0, 1.0);
	EXPECT_TRUE(segment->HasMarker());
}

TEST(MarkerPolicyTest, ExplicitReturnCancelsTheScheduledOne)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Keyframe);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 3000, &frame_index);

	// The break announces its length, so its return point is scheduled at
	// 4210 + 10000; an explicit return then arrives for 9000 instead
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::OUT, 4210, kCueDurationMs));
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::IN, 4210 + kCueDurationMs, 0, true));
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::IN, 9000, 0));

	Drive(pipeline, 3000, 20000, &frame_index);

	// The break returns where the explicit return said
	auto returned = FindSegmentCovering(pipeline, 9000.0);
	ASSERT_NE(returned, nullptr);
	EXPECT_NEAR(static_cast<double>(returned->GetStartTimestamp()) + returned->GetDurationMs(), 9000.0, 1.0);
	ASSERT_TRUE(returned->HasMarker());
	auto in_marker = returned->GetMarkers().back();
	ASSERT_NE(in_marker->GetParent(), nullptr);
	EXPECT_EQ(in_marker->GetParent()->GetTimestampMs(), 4210);

	// The scheduled one was cancelled, so nothing cuts at its position
	auto at_scheduled = FindSegmentCovering(pipeline, 4210.0 + kCueDurationMs);
	ASSERT_NE(at_scheduled, nullptr);
	EXPECT_FALSE(at_scheduled->HasMarker());
}

//------------------------------------------------------------------------------
// Late markers
//------------------------------------------------------------------------------

TEST(MarkerPolicyTest, LateMarkerKeepsItsAdvertisedTime)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Immediate);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 10000, &frame_index);

	// The position has passed, but by less than a segment: only the cut moves
	// onto the nearest position that can still cut; the advertised time stays
	auto marker = MakeCueMarker(CueEvent::CueType::OUT, 7000, kCueDurationMs);
	ASSERT_TRUE(std::get<0>(pipeline.policy->CanInsertMarker(marker)));
	ASSERT_TRUE(pipeline.policy->InsertMarker(marker));
	EXPECT_EQ(marker->GetTimestampMs(), 7000);

	Drive(pipeline, 10000, 14000, &frame_index);

	// The marker rides the segment its cut landed in, not the one its
	// advertised time points into
	auto segment = FindSegmentCovering(pipeline, 9900.0);
	ASSERT_NE(segment, nullptr);
	EXPECT_TRUE(segment->HasMarker());
}

TEST(MarkerPolicyTest, TooLateMarkerIsRefused)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Immediate);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 10000, &frame_index);

	// More than a segment late: refused so the caller learns the position is gone
	auto marker = MakeCueMarker(CueEvent::CueType::OUT, 3000, kCueDurationMs);
	EXPECT_FALSE(std::get<0>(pipeline.policy->CanInsertMarker(marker)));
	EXPECT_FALSE(pipeline.policy->InsertMarker(marker));
}

//------------------------------------------------------------------------------
// Discontinuity and the marker chain
//------------------------------------------------------------------------------

TEST(MarkerPolicyTest, DiscontinuitySettlesTheCoveredMarkers)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	ASSERT_TRUE(policy->InsertMarker(MakeCueMarker(CueEvent::CueType::OUT, 5000, kCueDurationMs)));

	// The force-closed segment covers the marker's position; it still rides it
	bmff::CompletedSegment forced;
	forced.number = 0;
	forced.start_timestamp_us = 4000000;
	forced.duration_us = 2000000;
	auto result = policy->OnDiscontinuity(forced);

	ASSERT_EQ(result.markers.size(), 1u);
	EXPECT_EQ(result.markers.front()->GetTimestampMs(), 5000);
}

TEST(MarkerPolicyTest, MarkerInAHoleRidesTheNextSegment)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	ASSERT_TRUE(policy->InsertMarker(MakeCueMarker(CueEvent::CueType::OUT, 5000, kCueDurationMs)));

	// The keyframe wait after a track change left a hole: the segment starts
	// past the marker's position. The marker still rides it.
	bmff::CompletedSegment completed;
	completed.number = 0;
	completed.start_timestamp_us = 5500000;
	completed.duration_us = 4000000;
	auto result = policy->OnSegmentCompleted(completed);
	ASSERT_EQ(result.markers.size(), 1u);
	EXPECT_EQ(result.markers.front()->GetTimestampMs(), 5000);

	// It cut nothing, so the pacing keeps the plain cadence instead of
	// discounting this segment
	EXPECT_DOUBLE_EQ(policy->GetSegmentBoundary(static_cast<int64_t>(0)).end_us / 1000.0, 4000.0);
}

TEST(MarkerPolicyTest, MarkerFurtherBehindThanASegmentIsDiscarded)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	ASSERT_TRUE(policy->InsertMarker(MakeCueMarker(CueEvent::CueType::OUT, 5000, kCueDurationMs)));

	// More than a segment behind the settled range: the advertised time no
	// longer describes where it would land, so it is dropped and the chain is
	// released for the next break
	bmff::CompletedSegment completed;
	completed.number = 0;
	completed.start_timestamp_us = 12000000;
	completed.duration_us = 4000000;
	auto result = policy->OnSegmentCompleted(completed);
	EXPECT_TRUE(result.markers.empty());

	EXPECT_TRUE(std::get<0>(policy->CanInsertMarker(MakeCueMarker(CueEvent::CueType::OUT, 20000, kCueDurationMs))));
}

TEST(MarkerPolicyTest, ExplicitInReplacesTheProvisionalReturnPoint)
{
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);

	ASSERT_TRUE(policy->InsertMarker(MakeCueMarker(CueEvent::CueType::OUT, 5000, kCueDurationMs)));

	// The provisional IN advertises 16000, but its cut follows the OUT: 15000
	ASSERT_TRUE(policy->InsertMarker(MakeCueMarker(CueEvent::CueType::IN, 16000, 0, true)));

	// An explicit IN may still replace it; the pending lookup must use the cut
	// position, not the advertised time
	auto explicit_in = MakeCueMarker(CueEvent::CueType::IN, 16500, 0);
	EXPECT_TRUE(std::get<0>(policy->CanInsertMarker(explicit_in)));
	EXPECT_TRUE(policy->InsertMarker(explicit_in));
}

TEST(MarkerPolicyTest, DiscontinuityOverridesAMarkerAimingAtTheSameCut)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Keyframe);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 5000, &frame_index);

	// Both aim at the 6000 ms keyframe: the break must take it, so the boundary
	// lands aligned across the renditions; the marker rides the closed segment
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::OUT, 5100, kCueDurationMs));
	pipeline.packager->RequestCutForDiscontinuity(5050.0);

	Drive(pipeline, 5000, 11000, &frame_index);

	auto cut_segment = FindSegmentCovering(pipeline, 5500.0);
	ASSERT_NE(cut_segment, nullptr);
	EXPECT_DOUBLE_EQ(static_cast<double>(cut_segment->GetStartTimestamp()) + cut_segment->GetDurationMs(), 6000.0);
	EXPECT_TRUE(cut_segment->HasMarker());

	auto next_segment = FindSegmentCovering(pipeline, 6500.0);
	ASSERT_NE(next_segment, nullptr);
	EXPECT_TRUE(next_segment->IsDiscontinuityPoint());
}

TEST(MarkerPolicyTest, PendingBreakRidesAnEarlierMarkerCut)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Immediate);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 5000, &frame_index);

	// The marker cuts mid-GOP at 5010, before the break at 5500. The track that
	// broke closes once, so this track must close once too: the break rides the
	// marker cut instead of adding a second boundary.
	ASSERT_TRUE(InsertCue(pipeline, CueEvent::CueType::OUT, 5010, kCueDurationMs));
	pipeline.packager->RequestCutForDiscontinuity(5500.0);

	Drive(pipeline, 5000, 12000, &frame_index);

	int discontinuity_count = 0;
	std::shared_ptr<bmff::FMP4Segment> break_segment;
	for (int64_t number = 0; number <= pipeline.storage->GetLastSegmentNumber(); number++)
	{
		auto segment = std::static_pointer_cast<bmff::FMP4Segment>(pipeline.storage->GetSegment(number));
		if (segment == nullptr || segment->IsDiscontinuityPoint() == false)
		{
			continue;
		}

		discontinuity_count++;
		break_segment = segment;
	}

	// Exactly one break, and it starts where the marker cut landed
	ASSERT_EQ(discontinuity_count, 1);
	ASSERT_NE(break_segment, nullptr);
	EXPECT_NEAR(static_cast<double>(break_segment->GetStartTimestamp()), 5010.0, 100.0 / 3.0 + 1.0);
}

TEST(MarkerPolicyTest, PropagatedDiscontinuityCutsAtTheNextKeyframe)
{
	auto pipeline = MakeVideoPipeline(LLHlsCueOutCutMode::Keyframe);
	int64_t frame_index = 0;

	Drive(pipeline, 0, 6000, &frame_index);

	// Another track changed at 6500 ms; this track cuts at its next keyframe
	// (7000). A second report of the same event is absorbed.
	pipeline.packager->RequestCutForDiscontinuity(6500.0);
	pipeline.packager->RequestCutForDiscontinuity(6800.0);

	Drive(pipeline, 6000, 12000, &frame_index);

	auto cut_segment = FindSegmentCovering(pipeline, 6500.0);
	ASSERT_NE(cut_segment, nullptr);
	EXPECT_DOUBLE_EQ(static_cast<double>(cut_segment->GetStartTimestamp()) + cut_segment->GetDurationMs(), 7000.0);

	auto next_segment = FindSegmentCovering(pipeline, 7500.0);
	ASSERT_NE(next_segment, nullptr);
	EXPECT_TRUE(next_segment->IsDiscontinuityPoint());
	EXPECT_EQ(next_segment->GetNumber(), cut_segment->GetNumber() + 1);
}

//------------------------------------------------------------------------------
// Duration mode across tracks
//------------------------------------------------------------------------------

namespace
{
	TrackPipeline MakeAudioPipeline()
	{
		auto track = fmp4_test::MakeAudioTrack(1);
		auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);
		return fmp4_test::MakePipeline(track, std::make_shared<fmp4_test::NullStorageObserver>(), policy,
									   kSegmentDurationMs, kChunkDurationMs, "marker_test_audio");
	}

	// The markers of one cue type, in playlist order: the segment they ride and
	// where that segment ends
	std::vector<std::pair<std::shared_ptr<Marker>, double>> MarkersOn(const TrackPipeline &pipeline, CueEvent::CueType type)
	{
		std::vector<std::pair<std::shared_ptr<Marker>, double>> found;

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
				if (cue_event != nullptr && cue_event->GetCueType() == type)
				{
					found.emplace_back(marker, static_cast<double>(segment->GetStartTimestamp()) + segment->GetDurationMs());
				}
			}
		}

		return found;
	}
}  // namespace

TEST(MarkerPolicyTest, EveryTrackCarriesEveryMarkerInDurationMode)
{
	// The shipping default: duration mode, keyframe cuts, video and audio side
	// by side. Segment numbers may differ between the tracks; what must hold is
	// that every rendition carries every tag, paired and in place.
	auto video = MakeVideoPipeline(LLHlsCueOutCutMode::Keyframe);
	auto audio = MakeAudioPipeline();

	const std::vector<int64_t> cue_out_timestamps = {13210, 33210, 53210};

	int64_t video_index = 0;
	int64_t audio_index = 0;
	size_t cue_step = 0;  // two steps per cue: the OUT, then its return point

	while (true)
	{
		const int64_t video_dts = video_index * 100 / 3;
		const int64_t audio_dts = audio_index * 64 / 3;
		const int64_t now_ms = std::min(video_dts, audio_dts);

		if (now_ms >= 70000)
		{
			break;
		}

		// Like the stream: the marker goes to every accepting track, or none
		const size_t cue_index = cue_step / 2;
		if (cue_index < cue_out_timestamps.size())
		{
			const bool is_out = (cue_step % 2 == 0);
			const int64_t position = cue_out_timestamps[cue_index] + (is_out ? 0 : static_cast<int64_t>(kCueDurationMs));
			if (now_ms >= position - 1000)
			{
				for (auto *pipeline : {&video, &audio})
				{
					ASSERT_TRUE(InsertCue(*pipeline, is_out ? CueEvent::CueType::OUT : CueEvent::CueType::IN, position, is_out ? kCueDurationMs : 0));
				}
				cue_step++;
			}
		}

		if (video_dts <= audio_dts)
		{
			const int64_t duration = (video_index + 1) * 100 / 3 - video_dts;
			ASSERT_TRUE(video.packager->AppendSample(fmp4_test::MakeVideoFrame(0, video_dts, duration, (video_index % kGopFrames) == 0)));
			video_index++;
		}
		else
		{
			const int64_t duration = (audio_index + 1) * 64 / 3 - audio_dts;
			ASSERT_TRUE(audio.packager->AppendSample(fmp4_test::MakeAudioFrame(1, audio_dts, duration)));
			audio_index++;
		}
	}

	ASSERT_EQ(cue_step, cue_out_timestamps.size() * 2);

	for (auto *pipeline : {&video, &audio})
	{
		auto outs = MarkersOn(*pipeline, CueEvent::CueType::OUT);
		auto ins = MarkersOn(*pipeline, CueEvent::CueType::IN);
		ASSERT_EQ(outs.size(), cue_out_timestamps.size());
		ASSERT_EQ(ins.size(), cue_out_timestamps.size());

		for (size_t index = 0; index < cue_out_timestamps.size(); index++)
		{
			// Each tag rides the segment that ends at its cut: video at the next
			// keyframe (1000 ms GOP), audio at the next audio frame
			const double out_position = static_cast<double>(cue_out_timestamps[index]);
			EXPECT_GE(outs[index].second, out_position) << "cue " << index;
			EXPECT_LT(outs[index].second, out_position + 1100.0) << "cue " << index;

			const double in_position = out_position + kCueDurationMs;
			EXPECT_GE(ins[index].second, in_position) << "cue " << index;
			EXPECT_LT(ins[index].second, in_position + 1100.0) << "cue " << index;

			// The return point belongs to its break
			ASSERT_NE(ins[index].first->GetParent(), nullptr) << "cue " << index;
			EXPECT_EQ(ins[index].first->GetParent()->GetTimestampMs(), cue_out_timestamps[index]) << "cue " << index;
		}
	}
}

//------------------------------------------------------------------------------
// Emission timing robustness
//------------------------------------------------------------------------------

TEST(EmissionTimingTest, PartCutsWaitForTheReorderToResolve)
{
	// Decode order I P B B: a cut right after the P would emit a frame that
	// displays after the B frames still buffered, so the part would cover
	// different presentation time than its declared duration says. Cuts may
	// land only where everything emitted displays before everything kept.
	auto track = fmp4_test::MakeVideoTrack(0, 10.0, 40);
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);
	auto pipeline = fmp4_test::MakePipeline(track, std::make_shared<fmp4_test::NullStorageObserver>(), policy,
										   kSegmentDurationMs, kChunkDurationMs, "reorder_test");

	constexpr int64_t kFrameMs = 100;
	constexpr int64_t kGroupMs = 300;  // one P B B reorder group
	constexpr int64_t kGopMs = 4000;

	for (int64_t index = 0; index < 120; index++)
	{
		int64_t dts = index * kFrameMs;
		int64_t local = index % 40;
		bool keyframe = (local == 0);

		int64_t pts;
		if (keyframe == true)
		{
			pts = dts;
		}
		else if ((local - 1) % 3 == 0)
		{
			// The P displays after the two B frames that follow it in decode order
			pts = dts + 2 * kFrameMs;
		}
		else
		{
			pts = dts - kFrameMs;
		}

		ASSERT_TRUE(pipeline.packager->AppendSample(fmp4_test::MakeVideoFrame(0, dts, kFrameMs, keyframe, pts)));
	}

	int checked_partials = 0;
	for (int64_t number = 0; number <= pipeline.storage->GetLastSegmentNumber(); number++)
	{
		auto segment = std::static_pointer_cast<bmff::FMP4Segment>(pipeline.storage->GetSegment(number));
		if (segment == nullptr || segment->IsCompleted() == false)
		{
			continue;
		}

		for (uint64_t index = 0; index < segment->GetPartialCount(); index++)
		{
			auto partial = segment->GetPartialSegment(index);
			ASSERT_NE(partial, nullptr);

			// Resolved positions are the group boundaries: the GOP start, and
			// one frame past each group start within it
			int64_t local_offset = partial->GetStartTimestamp() % kGopMs;
			EXPECT_TRUE(local_offset == 0 || (local_offset % kGroupMs) == kFrameMs)
				<< "the partial at " << partial->GetStartTimestamp() << " splits a reorder group";
			checked_partials++;
		}
	}

	EXPECT_GE(checked_partials, 20);
}

TEST(EmissionTimingTest, FractionalTicksKeepThePartCadence)
{
	// 60 fps in a 90 kHz timebase: 1500-tick frames stamp as 16.66..67 ms, so
	// per-sample sums pass the part target by float noise alone. The emission
	// decisions must not flip on it: every part stays 24 frames, every segment
	// stays exactly on target.
	auto track = fmp4_test::MakeVideoTrack(0, 60.0, 120, 90000);
	auto policy = std::make_shared<bmff::DurationBoundaryPolicy>(MakeConfig(), false);
	auto pipeline = fmp4_test::MakePipeline(track, std::make_shared<fmp4_test::NullStorageObserver>(), policy,
										   kSegmentDurationMs, kChunkDurationMs, "timing_test");
	auto &storage = pipeline.storage;
	auto &packager = pipeline.packager;

	for (int64_t index = 0; index < 60 * 12; index++)
	{
		ASSERT_TRUE(packager->AppendSample(fmp4_test::MakeVideoFrame(0, index * 1500, 1500, (index % 120) == 0)));
	}

	int completed = 0;
	for (int64_t number = 0; number <= storage->GetLastSegmentNumber(); number++)
	{
		auto segment = std::static_pointer_cast<bmff::FMP4Segment>(storage->GetSegment(number));
		if (segment == nullptr || segment->IsCompleted() == false)
		{
			continue;
		}

		completed++;
		EXPECT_DOUBLE_EQ(segment->GetDurationMs(), static_cast<double>(kSegmentDurationMs)) << "segment " << number;
		EXPECT_EQ(segment->GetPartialCount(), kSegmentDurationMs / static_cast<uint64_t>(kChunkDurationMs)) << "segment " << number;
	}
	EXPECT_GE(completed, 2);
}

