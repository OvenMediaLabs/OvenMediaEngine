#include <gtest/gtest.h>

#include "llhls_chunklist.h"

namespace
{
	std::shared_ptr<MediaTrack> CreateVideoTrack()
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(1);
		track->SetMediaType(cmn::MediaType::Video);
		track->SetPublicName("video");
		track->SetVariantName("video");
		return track;
	}

	std::shared_ptr<LLHlsChunklist> CreateChunklist(const std::shared_ptr<MediaTrack> &track)
	{
		return std::make_shared<LLHlsChunklist>("chunklist_1_video_key_llhls.m3u8", track,
												10,	 // segment count
												6,	 // target duration
												0.5, // part target duration
												"init_1_video_key_llhls.m4s", true);
	}

	// Simulates a segment packaged against the given track version, the way
	// LLHlsStream::OnMediaChunkUpdated stamps partial infos from the storage segment
	void AppendSegment(const std::shared_ptr<LLHlsChunklist> &chunklist, uint32_t sequence, uint32_t track_version, const ov::String &map_uri, bool discontinuity_point = false)
	{
		auto url = ov::String::FormatString("seg_1_%u_video_key_llhls.m4s", sequence);
		chunklist->CreateSegmentInfo(LLHlsChunklist::SegmentInfo(sequence, url));

		auto partial_url = ov::String::FormatString("part_1_%u_0_video_key_llhls.m4s", sequence);
		auto next_partial_url = ov::String::FormatString("part_1_%u_1_video_key_llhls.m4s", sequence);
		auto partial_info = LLHlsChunklist::SegmentInfo(0, sequence * 6000, 6.0, 1000, partial_url, next_partial_url, true, true);
		partial_info.SetTrackVersion(track_version);
		partial_info.SetMapUri(map_uri);
		if (discontinuity_point == true)
		{
			partial_info.SetDiscontinuity();
		}

		chunklist->AppendPartialSegmentInfo(sequence, partial_info);
	}

	const ov::String kInitialMapUri = "init_1_video_key_llhls.m4s";
	const ov::String kSecondMapUri = "init_1_video_key_v2_llhls.m4s";
} // namespace

TEST(LLHlsChunklist, NoDiscontinuityTagsByDefault)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 1, kInitialMapUri);

	auto playlist = chunklist->ToString("", false, false, false);

	EXPECT_EQ(playlist.IndexOf("#EXT-X-DISCONTINUITY"), -1);
	EXPECT_NE(playlist.IndexOf("#EXT-X-MAP:URI=\"init_1_video_key_llhls.m4s\""), -1);
}

TEST(LLHlsChunklist, TrackVersionChangeInsertsTagAndNewMap)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 1, kInitialMapUri);

	// The next segment was packaged against a new track configuration
	AppendSegment(chunklist, 2, 2, kSecondMapUri);

	auto playlist = chunklist->ToString("", false, false, false);

	auto discontinuity_index = playlist.IndexOf("#EXT-X-DISCONTINUITY\n");
	EXPECT_NE(discontinuity_index, -1);

	// The initial map leads the playlist, the new map follows the discontinuity
	auto initial_map_index = playlist.IndexOf("#EXT-X-MAP:URI=\"init_1_video_key_llhls.m4s\"");
	auto new_map_index = playlist.IndexOf("#EXT-X-MAP:URI=\"init_1_video_key_v2_llhls.m4s\"");
	EXPECT_NE(initial_map_index, -1);
	EXPECT_NE(new_map_index, -1);
	EXPECT_LT(initial_map_index, discontinuity_index);
	EXPECT_LT(discontinuity_index, new_map_index);

	// The new map precedes the segment it applies to
	EXPECT_LT(new_map_index, playlist.IndexOf("seg_1_2_video_key_llhls.m4s"));

	EXPECT_NE(playlist.IndexOf("#EXT-X-DISCONTINUITY-SEQUENCE:0\n"), -1);
}

TEST(LLHlsChunklist, DiscontinuitySequenceCountsRemovedTags)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 2, kSecondMapUri);
	AppendSegment(chunklist, 2, 2, kSecondMapUri);
	AppendSegment(chunklist, 3, 2, kSecondMapUri);

	// Removing the unflagged first segment keeps the sequence number
	chunklist->RemoveSegmentInfo(0);
	auto playlist = chunklist->ToString("", false, false, false);
	EXPECT_NE(playlist.IndexOf("#EXT-X-DISCONTINUITY-SEQUENCE:0\n"), -1);
	// The tag stays while its segment is the first of the playlist
	EXPECT_NE(playlist.IndexOf("#EXT-X-DISCONTINUITY\n"), -1);
	EXPECT_NE(playlist.IndexOf("#EXT-X-MAP:URI=\"init_1_video_key_v2_llhls.m4s\""), -1);

	// Removing the flagged segment removes its tag and increments the sequence
	chunklist->RemoveSegmentInfo(1);
	playlist = chunklist->ToString("", false, false, false);
	EXPECT_NE(playlist.IndexOf("#EXT-X-DISCONTINUITY-SEQUENCE:1\n"), -1);
	EXPECT_EQ(playlist.IndexOf("#EXT-X-DISCONTINUITY\n"), -1);
}

TEST(LLHlsChunklist, PropagatedBoundaryFlagsWithoutVersionChange)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);

	// Another track of the stream changed; this track was cut at the boundary but
	// keeps its own configuration and map
	AppendSegment(chunklist, 1, 1, kInitialMapUri, true);

	auto playlist = chunklist->ToString("", false, false, false);

	auto discontinuity_index = playlist.IndexOf("#EXT-X-DISCONTINUITY\n");
	EXPECT_NE(discontinuity_index, -1);
	EXPECT_LT(discontinuity_index, playlist.IndexOf("seg_1_1_video_key_llhls.m4s"));

	// The map does not change, so only one EXT-X-MAP appears
	auto first_map_index = playlist.IndexOf("#EXT-X-MAP:URI=\"init_1_video_key_llhls.m4s\"");
	EXPECT_NE(first_map_index, -1);
	EXPECT_EQ(playlist.IndexOf("#EXT-X-MAP", first_map_index + 1), -1);
}

TEST(LLHlsChunklist, BoundaryCutHintsUpcomingMap)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 1, kInitialMapUri);

	// Boundary cut: segment 1 is completed and the next partial opens a new map
	chunklist->CompleteSegmentInfo(1, "part_1_2_0_video_key_llhls.m4s", kSecondMapUri);

	auto playlist = chunklist->ToString("", false, false, false);

	// The upcoming map is hinted ahead of the part that will use it
	auto map_hint_index = playlist.IndexOf("#EXT-X-PRELOAD-HINT:TYPE=MAP,URI=\"init_1_video_key_v2_llhls.m4s\"");
	auto part_hint_index = playlist.IndexOf("#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"part_1_2_0_video_key_llhls.m4s\"");
	EXPECT_NE(map_hint_index, -1);
	EXPECT_NE(part_hint_index, -1);
	EXPECT_LT(map_hint_index, part_hint_index);

	// Once the first partial of the new domain is listed the map appears inline
	AppendSegment(chunklist, 2, 2, kSecondMapUri);
	playlist = chunklist->ToString("", false, false, false);
	EXPECT_EQ(playlist.IndexOf("#EXT-X-PRELOAD-HINT:TYPE=MAP"), -1);
	EXPECT_NE(playlist.IndexOf("#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"part_1_2_1_video_key_llhls.m4s\""), -1);
}

TEST(LLHlsChunklist, UpcomingMapHintSetDirectly)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	// The boundary flush completed segment 1 through the normal chunk path; the
	// stream sets the upcoming map once the new initialization section is stored
	AppendSegment(chunklist, 1, 1, kInitialMapUri);
	chunklist->SetUpcomingMapUri(kSecondMapUri);

	auto playlist = chunklist->ToString("", false, false, false);
	EXPECT_NE(playlist.IndexOf("#EXT-X-PRELOAD-HINT:TYPE=MAP,URI=\"init_1_video_key_v2_llhls.m4s\""), -1);

	// The first partial of the new domain retires the map hint
	AppendSegment(chunklist, 2, 2, kSecondMapUri);
	playlist = chunklist->ToString("", false, false, false);
	EXPECT_EQ(playlist.IndexOf("#EXT-X-PRELOAD-HINT:TYPE=MAP"), -1);
}

TEST(LLHlsChunklist, PropagatedBoundaryCutDoesNotHintMap)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 1, kInitialMapUri);

	// A cut propagated from another track keeps this track's map
	chunklist->CompleteSegmentInfo(1, "part_1_2_0_video_key_llhls.m4s", kInitialMapUri);

	auto playlist = chunklist->ToString("", false, false, false);

	// No map change, so no map hint; the redirected part hint stays
	EXPECT_EQ(playlist.IndexOf("#EXT-X-PRELOAD-HINT:TYPE=MAP"), -1);
	EXPECT_NE(playlist.IndexOf("#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"part_1_2_0_video_key_llhls.m4s\""), -1);
}

TEST(LLHlsChunklist, VersionChangeBeforeFirstSegmentHasNoTag)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	// The track changed before anything was published; the first segment simply
	// starts with the new configuration
	AppendSegment(chunklist, 0, 2, kSecondMapUri);
	AppendSegment(chunklist, 1, 2, kSecondMapUri);

	auto playlist = chunklist->ToString("", false, false, false);

	EXPECT_EQ(playlist.IndexOf("#EXT-X-DISCONTINUITY"), -1);
	EXPECT_NE(playlist.IndexOf("#EXT-X-MAP:URI=\"init_1_video_key_v2_llhls.m4s\""), -1);
	EXPECT_EQ(playlist.IndexOf("#EXT-X-MAP:URI=\"init_1_video_key_llhls.m4s\""), -1);
}
