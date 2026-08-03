//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <vector>

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/modules/data_format/cue_event/cue_event.h>

#include "fmp4_packager.h"
#include "fmp4_storage.h"

// Cue markers must keep every track on the same segment sequence: the OUT marker
// cuts all tracks at its timestamp, the IN marker returns them to the shared
// segment grid, and the pacing gives each track exactly one extra boundary per
// marker. This holds when the keyframe interval is strictly shorter than the
// segment duration and divides it evenly; these tests drive the supported
// configurations with the real packagers and verify the invariants over many
// cues. An OUT and its return(IN) arrive as separate events.

namespace
{
	constexpr double kChunkDurationMs = 400.0;
	constexpr uint32_t kCueDurationMs = 20000;
	constexpr int64_t kCueIntervalMs = 30000;
	constexpr size_t kCueCount = 12;
	constexpr double kFrameJitterMs = 45.0;	 // integer-ms timestamps wobble up to one frame

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
		std::shared_ptr<bmff::FMP4Storage> storage;
		std::shared_ptr<bmff::FMP4Packager> packager;
	};

	TrackPipeline MakePipeline(const std::shared_ptr<MediaTrack> &track, const std::shared_ptr<bmff::FMp4StorageObserver> &observer, double segment_duration_ms)
	{
		bmff::FMP4Storage::Config storage_config;
		storage_config.max_segments = 100000;  // keep everything for inspection
		storage_config.segment_duration_ms = static_cast<uint64_t>(segment_duration_ms);

		bmff::FMP4Packager::Config packager_config;
		packager_config.chunk_duration_ms = kChunkDurationMs;
		packager_config.segment_duration_ms = segment_duration_ms;

		TrackPipeline pipeline;
		pipeline.track = track;
		pipeline.storage = std::make_shared<bmff::FMP4Storage>(observer, track, storage_config, "marker_sync_test");
		pipeline.packager = std::make_shared<bmff::FMP4Packager>(pipeline.storage, track, nullptr, packager_config);
		return pipeline;
	}

	std::shared_ptr<MediaTrack> MakeVideoTrack(int gop_frames)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(0);
		track->SetMediaType(cmn::MediaType::Video);
		track->SetCodecId(cmn::MediaCodecId::H264);
		track->SetTimeBase(1, 1000);
		track->SetFrameRateByConfig(30.0);
		track->SetKeyFrameIntervalByConfig(gop_frames);
		return track;
	}

	std::shared_ptr<MediaTrack> MakeAudioTrack()
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(1);
		track->SetMediaType(cmn::MediaType::Audio);
		track->SetCodecId(cmn::MediaCodecId::Aac);
		track->SetTimeBase(1, 1000);
		return track;
	}

	std::shared_ptr<MediaPacket> MakeVideoFrame(int64_t dts, int64_t duration, bool keyframe)
	{
		auto data = std::make_shared<ov::Data>();
		uint8_t byte = 0x00;
		data->Append(&byte, 1);
		return std::make_shared<MediaPacket>(cmn::MediaType::Video, 0, data, dts, dts, duration,
											 keyframe ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
											 cmn::BitstreamFormat::H264_AVCC, cmn::PacketType::NALU);
	}

	std::shared_ptr<MediaPacket> MakeAudioFrame(int64_t dts, int64_t duration)
	{
		auto data = std::make_shared<ov::Data>();
		uint8_t byte = 0x00;
		data->Append(&byte, 1);
		return std::make_shared<MediaPacket>(cmn::MediaType::Audio, 1, data, dts, dts, duration,
											 MediaPacketFlag::Key, cmn::BitstreamFormat::AAC_RAW, cmn::PacketType::RAW);
	}

	// Same protocol as LLHlsStream::InsertMarkerToAllPackagers: one sequence number,
	// estimated by the video packager and lifted to the highest current sequence,
	// shared by every track
	bool InsertCueMarker(const TrackPipeline &video, const TrackPipeline &audio, CueEvent::CueType type, int64_t timestamp_ms, uint32_t duration_ms, bool provisional = false)
	{
		auto data = CueEvent::Create(type, duration_ms, 0, provisional)->Serialize();

		int64_t estimated_seq = video.packager->GetEstimatedSequenceNumber(timestamp_ms);
		int64_t max_current_seq = std::max(video.packager->GetCurrentSequenceNumber(), audio.packager->GetCurrentSequenceNumber());

		for (const auto &pipeline : {video, audio})
		{
			auto marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, timestamp_ms, timestamp_ms, data);
			auto [can_insert, message] = pipeline.packager->CanInsertMarker(marker);
			if (can_insert == false)
			{
				ADD_FAILURE() << "CanInsertMarker failed: " << message.CStr();
				return false;
			}
		}

		if (max_current_seq > estimated_seq)
		{
			estimated_seq = max_current_seq;
		}

		for (const auto &pipeline : {video, audio})
		{
			auto marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, timestamp_ms, timestamp_ms, data);
			marker->SetDesiredSequenceNumber(estimated_seq);
			if (pipeline.packager->InsertMarker(marker) == false)
			{
				ADD_FAILURE() << "InsertMarker failed at " << timestamp_ms;
				return false;
			}
		}

		return true;
	}

	struct MarkerObservation
	{
		int64_t segment_number = -1;
		double segment_start_ms = 0.0;
		double segment_duration_ms = 0.0;
	};

	// cue index (by order of appearance) -> observation
	std::map<size_t, MarkerObservation> CollectMarkers(const TrackPipeline &pipeline, CueEvent::CueType type)
	{
		std::map<size_t, MarkerObservation> observations;
		size_t index = 0;

		for (int64_t number = 0; number <= pipeline.storage->GetLastSegmentNumber(); number++)
		{
			auto segment = pipeline.storage->GetSegment(number);
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

				observations[index++] = {segment->GetNumber(), static_cast<double>(segment->GetStartTimestamp()), segment->GetDurationMs()};
			}
		}

		return observations;
	}

	void RunScenario(double segment_duration_ms, int gop_frames)
	{
		const double gop_ms = gop_frames * 100.0 / 3.0;
		SCOPED_TRACE(::testing::Message() << "segment " << segment_duration_ms << " ms, gop " << gop_ms << " ms");

		auto observer = std::make_shared<NullStorageObserver>();
		auto video = MakePipeline(MakeVideoTrack(gop_frames), observer, segment_duration_ms);
		auto audio = MakePipeline(MakeAudioTrack(), observer, segment_duration_ms);

		// Marker events are stamped one keyframe interval ahead of the media time
		const int64_t marker_lead_ms = static_cast<int64_t>(gop_ms);

		std::vector<int64_t> cue_out_timestamps;
		for (size_t index = 0; index < kCueCount; index++)
		{
			cue_out_timestamps.push_back(3000 + static_cast<int64_t>(index) * kCueIntervalMs + marker_lead_ms);
		}

		const int64_t total_duration_ms = cue_out_timestamps.back() + kCueDurationMs + 2 * static_cast<int64_t>(segment_duration_ms) + 5000;

		// RTMP-style integer millisecond timestamps
		int64_t video_index = 0;
		int64_t audio_index = 0;
		size_t cue_index = 0;

		while (true)
		{
			const int64_t video_dts = video_index * 100 / 3;
			const int64_t audio_dts = audio_index * 64 / 3;
			const int64_t now_ms = std::min(video_dts, audio_dts);

			if (now_ms >= total_duration_ms)
			{
				break;
			}

			// The OUT and its paired provisional IN arrive back to back, markers
			// ahead of the media time
			if (cue_index < kCueCount && now_ms >= cue_out_timestamps[cue_index] - marker_lead_ms)
			{
				ASSERT_TRUE(InsertCueMarker(video, audio, CueEvent::CueType::OUT, cue_out_timestamps[cue_index], kCueDurationMs));
				ASSERT_TRUE(InsertCueMarker(video, audio, CueEvent::CueType::IN, cue_out_timestamps[cue_index] + kCueDurationMs, 0, true));

				// An explicit IN for the same return point must replace the pending
				// one harmlessly, on the same sequence
				ASSERT_TRUE(InsertCueMarker(video, audio, CueEvent::CueType::IN, cue_out_timestamps[cue_index] + kCueDurationMs, 0));
				cue_index++;
			}

			if (video_dts <= audio_dts)
			{
				const int64_t duration = (video_index + 1) * 100 / 3 - video_dts;
				ASSERT_TRUE(video.packager->AppendSample(MakeVideoFrame(video_dts, duration, (video_index % gop_frames) == 0)));
				video_index++;
			}
			else
			{
				const int64_t duration = (audio_index + 1) * 64 / 3 - audio_dts;
				ASSERT_TRUE(audio.packager->AppendSample(MakeAudioFrame(audio_dts, duration)));
				audio_index++;
			}
		}

		ASSERT_EQ(cue_index, kCueCount);

		auto video_out = CollectMarkers(video, CueEvent::CueType::OUT);
		auto audio_out = CollectMarkers(audio, CueEvent::CueType::OUT);
		auto video_in = CollectMarkers(video, CueEvent::CueType::IN);
		auto audio_in = CollectMarkers(audio, CueEvent::CueType::IN);

		ASSERT_EQ(video_out.size(), kCueCount);
		ASSERT_EQ(audio_out.size(), kCueCount);
		ASSERT_EQ(video_in.size(), kCueCount);
		ASSERT_EQ(audio_in.size(), kCueCount);

		for (size_t index = 0; index < kCueCount; index++)
		{
			const double out_ts = static_cast<double>(cue_out_timestamps[index]);
			const double in_ts = out_ts + kCueDurationMs;

			const auto &v_out = video_out[index];
			const auto &a_out = audio_out[index];

			// The OUT marker cuts every track at its timestamp, on the same sequence
			EXPECT_EQ(v_out.segment_number, a_out.segment_number) << "OUT marker of cue " << index << " landed on different sequences";
			EXPECT_NEAR(v_out.segment_start_ms + v_out.segment_duration_ms, out_ts, kFrameJitterMs) << "video was not cut at cue " << index;
			EXPECT_NEAR(a_out.segment_start_ms + a_out.segment_duration_ms, out_ts, kFrameJitterMs) << "audio was not cut at cue " << index;

			const auto &v_in = video_in[index];
			const auto &a_in = audio_in[index];

			// The IN marker lands on the same sequence on every track. Audio returns
			// at the timestamp itself, video at the first keyframe at or after it.
			EXPECT_EQ(v_in.segment_number, a_in.segment_number) << "IN marker of cue " << index << " landed on different sequences";
			EXPECT_NEAR(a_in.segment_start_ms + a_in.segment_duration_ms, in_ts, kFrameJitterMs) << "audio did not return at cue " << index;

			const double v_in_end = v_in.segment_start_ms + v_in.segment_duration_ms;
			EXPECT_GE(v_in_end, in_ts - kFrameJitterMs) << "video returned before cue " << index << " ended";
			EXPECT_LE(v_in_end, in_ts + gop_ms + kFrameJitterMs) << "video return exceeded one keyframe interval at cue " << index;
		}

		// The sequences must not drift apart over the whole run
		EXPECT_LE(std::abs(video.storage->GetLastSegmentNumber() - audio.storage->GetLastSegmentNumber()), 1);

		// A segment waiting for a marker may stretch up to one keyframe interval
		// past the target, but the force-completion threshold (twice the target)
		// must never be reached
		for (const auto &pipeline : {video, audio})
		{
			for (int64_t number = 0; number <= pipeline.storage->GetLastSegmentNumber(); number++)
			{
				auto segment = pipeline.storage->GetSegment(number);
				if (segment == nullptr || segment->IsCompleted() == false)
				{
					continue;
				}

				EXPECT_LE(segment->GetDurationMs(), segment_duration_ms + gop_ms + kFrameJitterMs)
					<< "track " << pipeline.track->GetId() << " segment " << number << " was force-completed or overstretched";
			}
		}
	}
}  // namespace

// One cue driven through the real packagers with an adjustable feed end
namespace
{
	struct SingleCueRun
	{
		TrackPipeline video;
		TrackPipeline audio;

		void Feed(int gop_frames, int64_t until_ms, int64_t out_ts_ms, uint32_t duration_ms, int64_t extension_at_ms, int64_t extension_ts_ms, bool paired_in_provisional)
		{
			const int64_t marker_lead_ms = static_cast<int64_t>(gop_frames * 100.0 / 3.0);

			int64_t video_index = 0;
			int64_t audio_index = 0;
			bool cue_sent = false;
			bool extension_sent = (extension_at_ms < 0);

			while (true)
			{
				const int64_t video_dts = video_index * 100 / 3;
				const int64_t audio_dts = audio_index * 64 / 3;
				const int64_t now_ms = std::min(video_dts, audio_dts);

				if (now_ms >= until_ms)
				{
					break;
				}

				if (cue_sent == false && now_ms >= out_ts_ms - marker_lead_ms)
				{
					ASSERT_TRUE(InsertCueMarker(video, audio, CueEvent::CueType::OUT, out_ts_ms, duration_ms));
					ASSERT_TRUE(InsertCueMarker(video, audio, CueEvent::CueType::IN, out_ts_ms + duration_ms, 0, paired_in_provisional));
					cue_sent = true;
				}

				if (extension_sent == false && now_ms >= extension_at_ms)
				{
					ASSERT_TRUE(InsertCueMarker(video, audio, CueEvent::CueType::IN, extension_ts_ms, 0));
					extension_sent = true;
				}

				if (video_dts <= audio_dts)
				{
					const int64_t duration = (video_index + 1) * 100 / 3 - video_dts;
					ASSERT_TRUE(video.packager->AppendSample(MakeVideoFrame(video_dts, duration, (video_index % gop_frames) == 0)));
					video_index++;
				}
				else
				{
					const int64_t duration = (audio_index + 1) * 64 / 3 - audio_dts;
					ASSERT_TRUE(audio.packager->AppendSample(MakeAudioFrame(audio_dts, duration)));
					audio_index++;
				}
			}
		}
	};
}  // namespace

TEST(FMP4MarkerSyncTest, ProvisionalInCanBeExtended)
{
	constexpr double kSegmentMs = 1600.0;
	constexpr int kGopFrames = 24;	// 800 ms
	constexpr double kGopMs = 800.0;

	auto observer = std::make_shared<NullStorageObserver>();
	SingleCueRun run{MakePipeline(MakeVideoTrack(kGopFrames), observer, kSegmentMs), MakePipeline(MakeAudioTrack(), observer, kSegmentMs)};

	// OUT at 4.8 s with a planned 20 s break; at 15 s the operator moves the
	// return point 5 s later than planned
	const int64_t out_ts = 4800;
	const int64_t planned_in_ts = out_ts + 20000;
	const int64_t extended_in_ts = planned_in_ts + 5000;
	run.Feed(kGopFrames, extended_in_ts + 10000, out_ts, 20000, 15000, extended_in_ts, true);

	auto video_in = CollectMarkers(run.video, CueEvent::CueType::IN);
	auto audio_in = CollectMarkers(run.audio, CueEvent::CueType::IN);

	// Exactly one IN, at the extended position, on the same sequence
	ASSERT_EQ(video_in.size(), 1u);
	ASSERT_EQ(audio_in.size(), 1u);
	EXPECT_EQ(video_in[0].segment_number, audio_in[0].segment_number);
	EXPECT_NEAR(audio_in[0].segment_start_ms + audio_in[0].segment_duration_ms, extended_in_ts, kFrameJitterMs);

	const double v_in_end = video_in[0].segment_start_ms + video_in[0].segment_duration_ms;
	EXPECT_GE(v_in_end, extended_in_ts - kFrameJitterMs);
	EXPECT_LE(v_in_end, extended_in_ts + kGopMs + kFrameJitterMs);
}

TEST(FMP4MarkerSyncTest, ConfirmedInRejectsExtension)
{
	constexpr double kSegmentMs = 1600.0;
	constexpr int kGopFrames = 24;

	auto observer = std::make_shared<NullStorageObserver>();
	SingleCueRun run{MakePipeline(MakeVideoTrack(kGopFrames), observer, kSegmentMs), MakePipeline(MakeAudioTrack(), observer, kSegmentMs)};

	// The IN of an autoReturn OUT is confirmed: the return point may only move earlier
	const int64_t out_ts = 4800;
	const int64_t planned_in_ts = out_ts + 20000;
	run.Feed(kGopFrames, 15000, out_ts, 20000, -1, 0, false);

	auto data = CueEvent::Create(CueEvent::CueType::IN)->Serialize();
	auto marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, planned_in_ts + 5000, planned_in_ts + 5000, data);
	auto [can_insert, message] = run.video.packager->CanInsertMarker(marker);
	EXPECT_FALSE(can_insert);

	// Moving it earlier stays allowed
	auto earlier_marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, planned_in_ts - 5000, planned_in_ts - 5000, data);
	auto [can_insert_earlier, message_earlier] = run.video.packager->CanInsertMarker(earlier_marker);
	EXPECT_TRUE(can_insert_earlier);
}

TEST(FMP4MarkerSyncTest, ProvisionalInRejectsProvisionalExtension)
{
	constexpr double kSegmentMs = 1600.0;
	constexpr int kGopFrames = 24;

	auto observer = std::make_shared<NullStorageObserver>();
	SingleCueRun run{MakePipeline(MakeVideoTrack(kGopFrames), observer, kSegmentMs), MakePipeline(MakeAudioTrack(), observer, kSegmentMs)};

	// A duplicate OUT sent mid-break is rejected, but its provisional companion IN
	// still arrives; it must not move the pending return point
	const int64_t out_ts = 4800;
	const int64_t planned_in_ts = out_ts + 20000;
	run.Feed(kGopFrames, 15000, out_ts, 20000, -1, 0, true);

	auto provisional_data = CueEvent::Create(CueEvent::CueType::IN, 0, 0, true)->Serialize();
	auto provisional_marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, planned_in_ts + 5000, planned_in_ts + 5000, provisional_data);
	auto [can_insert, message] = run.video.packager->CanInsertMarker(provisional_marker);
	EXPECT_FALSE(can_insert);

	// An explicit IN may still extend the pending provisional return point
	auto explicit_data = CueEvent::Create(CueEvent::CueType::IN)->Serialize();
	auto explicit_marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, planned_in_ts + 5000, planned_in_ts + 5000, explicit_data);
	auto [can_insert_explicit, message_explicit] = run.video.packager->CanInsertMarker(explicit_marker);
	EXPECT_TRUE(can_insert_explicit);
}

TEST(FMP4MarkerSyncTest, HalfSegmentGop)
{
	// 1.6 s segments, 0.8 s keyframe interval
	RunScenario(1600.0, 24);
}

TEST(FMP4MarkerSyncTest, HalfSegmentGopLarge)
{
	// 3.2 s segments, 1.6 s keyframe interval
	RunScenario(3200.0, 48);
}

TEST(FMP4MarkerSyncTest, QuarterSegmentGop)
{
	// 4 s segments, 1 s keyframe interval
	RunScenario(4000.0, 30);
}
