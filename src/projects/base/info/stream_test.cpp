//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/info/media_track.h>
#include <base/info/stream.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// Tests for `info::Stream` thread-safety fixes on this branch:
// `_source_url` is rewritten on every pull-stream failover (`SetMediaSource()`)
// while monitoring/serdes/publisher threads read it concurrently;
// both accessors now synchronize on `_source_url_mutex`.
// These tests pin the value-integrity contract
// (a reader/copier can only ever observe a fully written value, never a torn one)
// and are intended to run under TSan as well,
// where the pre-fix code would report a data race.

namespace
{
	const ov::String kUrlA = "ovt://primary.example.com:9000/app/stream";
	const ov::String kUrlB = "ovt://backup.example.com:9000/app/stream-with-a-long-tail-to-force-heap-allocation";

	bool IsExpectedValue(const ov::String &value)
	{
		// Empty is allowed only before the writer has stored anything
		return value.IsEmpty() || (value == kUrlA) || (value == kUrlB);
	}
}  // namespace

TEST(InfoStreamSourceUrl, ConcurrentSetAndGetObservesOnlyWholeValues)
{
	info::Stream stream(StreamSourceType::Ovt);

	std::atomic<bool> stop{false};
	std::atomic<uint64_t> read_count{0};
	std::atomic<bool> all_reads_valid{true};

	std::thread writer([&] {
		bool flip = false;
		while (stop.load() == false)
		{
			stream.SetMediaSource(flip ? kUrlA : kUrlB);
			flip = !flip;
		}
	});

	std::vector<std::thread> readers;
	for (int i = 0; i < 4; i++)
	{
		readers.emplace_back([&] {
			while (stop.load() == false)
			{
				auto value = stream.GetMediaSource();
				if (IsExpectedValue(value) == false)
				{
					all_reads_valid = false;
				}
				read_count++;
			}
		});
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	stop = true;

	writer.join();
	for (auto &reader : readers)
	{
		reader.join();
	}

	EXPECT_TRUE(all_reads_valid.load());
	EXPECT_GT(read_count.load(), 0u);
}

TEST(InfoStreamSourceUrl, CopyConstructorRacingWriterObservesOnlyWholeValues)
{
	// The copy path reads the source's `_source_url` via `GetMediaSource()` (locked);
	// copying while a failover-style writer is running must never yield a torn string.
	info::Stream stream(StreamSourceType::Ovt);
	stream.SetMediaSource(kUrlA);

	std::atomic<bool> stop{false};
	std::atomic<uint64_t> copy_count{0};
	std::atomic<bool> all_copies_valid{true};

	std::thread writer([&] {
		bool flip = false;
		while (stop.load() == false)
		{
			stream.SetMediaSource(flip ? kUrlA : kUrlB);
			flip = !flip;
		}
	});

	std::thread copier([&] {
		while (stop.load() == false)
		{
			info::Stream copy(stream);
			if (IsExpectedValue(copy.GetMediaSource()) == false)
			{
				all_copies_valid = false;
			}
			copy_count++;
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	stop = true;

	writer.join();
	copier.join();

	EXPECT_TRUE(all_copies_valid.load());
	EXPECT_GT(copy_count.load(), 0u);
}

// Regression test for the fix on this branch:
// `GetTrackByLabel()` is only ever used by callers expecting a *subtitle* track back
// (STT output routing, the sendSubtitles API, subtitle commands). Before the fix, the
// lookup map was populated from every track's PublicName regardless of media type, so an
// audio track sharing a name with a subtitle rendition (e.g. an <AudioMap> <Item><Name>
// matching a <Subtitles><Rendition><Label>, as happens with Scheduler + AudioMap) would
// shadow the subtitle track and make it unreachable by label - which in turn made
// pvd::Application::AddStream() believe the subtitle rendition already existed and skip
// creating it entirely.
TEST(InfoStreamTrackByLabel, SubtitleLabelIsNotShadowedByNonSubtitleTrackWithSameName)
{
	const ov::String kSharedLabel = "American English";

	info::Stream stream(StreamSourceType::Ovt);

	auto audio_track = std::make_shared<MediaTrack>();
	audio_track->SetId(stream.IssueUniqueTrackId());
	audio_track->SetMediaType(cmn::MediaType::Audio);
	audio_track->SetPublicName(kSharedLabel);
	ASSERT_TRUE(stream.AddTrack(audio_track));

	auto subtitle_track = std::make_shared<MediaTrack>();
	subtitle_track->SetId(stream.IssueUniqueTrackId());
	subtitle_track->SetMediaType(cmn::MediaType::Subtitle);
	subtitle_track->SetPublicName(kSharedLabel);
	ASSERT_TRUE(stream.AddTrack(subtitle_track));

	auto found = stream.GetTrackByLabel(kSharedLabel);
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->GetId(), subtitle_track->GetId());
	EXPECT_EQ(found->GetMediaType(), cmn::MediaType::Subtitle);
}
