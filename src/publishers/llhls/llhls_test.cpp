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
	void AppendSegment(const std::shared_ptr<LLHlsChunklist> &chunklist, uint32_t sequence, uint32_t track_version, const ov::String &map_uri, bool discontinuity_point = false, const ov::String &codecs = "")
	{
		auto url = ov::String::FormatString("seg_1_%u_video_key_llhls.m4s", sequence);
		chunklist->CreateSegmentInfo(LLHlsChunklist::SegmentInfo(sequence, url));

		auto partial_url = ov::String::FormatString("part_1_%u_0_video_key_llhls.m4s", sequence);
		auto next_partial_url = ov::String::FormatString("part_1_%u_1_video_key_llhls.m4s", sequence);
		auto partial_info = LLHlsChunklist::SegmentInfo(0, sequence * 6000, 6.0, 1000, partial_url, next_partial_url, true, true);
		partial_info.SetTrackVersion(track_version);
		partial_info.SetMapUri(map_uri);
		partial_info.SetCodecsParameter(codecs);
		if (discontinuity_point == true)
		{
			partial_info.SetDiscontinuity();
		}

		chunklist->AppendPartialSegmentInfo(sequence, partial_info);
	}

	const ov::String kInitialMapUri = "init_1_video_key_llhls.m4s";
	const ov::String kSecondMapUri = "init_1_video_key_v2_llhls.m4s";

	// A cbcs Widevine key. Distinct key_ids produce distinct EXT-X-KEY tags (the
	// KEYID attribute), the way a DRM key rotation registers each content version.
	bmff::CencProperty MakeCencProperty(const char *key_id_hex)
	{
		bmff::CencProperty cenc_property;
		cenc_property.scheme = bmff::CencProtectScheme::Cbcs;
		cenc_property.key_id = ov::Hex::Decode(key_id_hex);
		cenc_property.key = ov::Hex::Decode("16cf4232a86364b519e1982a27d90087");
		cenc_property.iv = ov::Hex::Decode("572547f914e34dc68ba9ba9ef91d4c4a");
		cenc_property.pssh_box_list.push_back(bmff::PsshBox("edef8ba9-79d6-4ace-a3c8-27dcd51d21ed", {cenc_property.key_id}, nullptr));
		return cenc_property;
	}

	const char *kKeyIdA = "572543f964e34dc68ba9ba9ef91d4c4a";
	const char *kKeyIdB = "a1b2c3d4e5f60718293a4b5c6d7e8f90";
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

	// The next segment was packaged against a new track configuration. A real track
	// change flags the boundary segment as a discontinuity point (the storage marks
	// it), which is what the chunklist turns into an EXT-X-DISCONTINUITY.
	AppendSegment(chunklist, 2, 2, kSecondMapUri, true);

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
	AppendSegment(chunklist, 1, 2, kSecondMapUri, true);
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

TEST(LLHlsChunklist, CodecsUnionFollowsListing)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri, false, "avc1.42c00d");
	AppendSegment(chunklist, 1, 2, kSecondMapUri, false, "avc1.640029");

	// Both versions are listed
	EXPECT_STREQ(chunklist->GetListedCodecsUnion().CStr(), "avc1.42c00d,avc1.640029");

	// The old version left the listing, its codecs leave the listed union but
	// stay in the learned history for dumped output
	chunklist->RemoveSegmentInfo(0);
	EXPECT_STREQ(chunklist->GetListedCodecsUnion().CStr(), "avc1.640029");
	EXPECT_STREQ(chunklist->GetAllCodecsUnion().CStr(), "avc1.42c00d,avc1.640029");
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

TEST(LLHlsChunklist, SingleKeyEmittedOnce)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());
	chunklist->EnableCenc(1, MakeCencProperty(kKeyIdA));

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 1, kInitialMapUri);

	auto playlist = chunklist->ToString("", false, false, false);

	// Exactly one EXT-X-KEY for the single content version
	auto first_key = playlist.IndexOf("#EXT-X-KEY:");
	EXPECT_NE(first_key, -1);
	EXPECT_EQ(playlist.IndexOf("#EXT-X-KEY:", first_key + 1), -1);
}

TEST(LLHlsChunklist, KeyRotationEmitsNewKeyWithoutDiscontinuity)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());
	chunklist->EnableCenc(1, MakeCencProperty(kKeyIdA));
	chunklist->EnableCenc(2, MakeCencProperty(kKeyIdB));

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 1, kInitialMapUri);

	// A key rotation: a new content version and map, but no discontinuity flag
	AppendSegment(chunklist, 2, 2, kSecondMapUri);

	auto playlist = chunklist->ToString("", false, false, false);

	// A key rotation is seamless: no discontinuity tag
	EXPECT_EQ(playlist.IndexOf("#EXT-X-DISCONTINUITY"), -1);

	// The new initialization section still switches in
	EXPECT_NE(playlist.IndexOf("#EXT-X-MAP:URI=\"init_1_video_key_v2_llhls.m4s\""), -1);

	// Both keys are advertised, the new one after the old one
	auto key_a = ov::String::FormatString("KEYID=0x%s", ov::String(kKeyIdA).UpperCaseString().CStr());
	auto key_b = ov::String::FormatString("KEYID=0x%s", ov::String(kKeyIdB).UpperCaseString().CStr());
	auto key_a_index = playlist.IndexOf(key_a.CStr());
	auto key_b_index = playlist.IndexOf(key_b.CStr());
	EXPECT_NE(key_a_index, -1);
	EXPECT_NE(key_b_index, -1);
	EXPECT_LT(key_a_index, key_b_index);

	// The new key precedes the segment it applies to
	EXPECT_LT(key_b_index, playlist.IndexOf("seg_1_2_video_key_llhls.m4s"));
}

TEST(LLHlsChunklist, TrackChangeKeepingTheKeyDoesNotRepeatIt)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	// A scheduled channel changes tracks often; those versions keep the current key
	chunklist->EnableCenc(1, MakeCencProperty(kKeyIdA));
	chunklist->EnableCenc(2, MakeCencProperty(kKeyIdA));
	// Then a rotation brings a new key
	chunklist->EnableCenc(3, MakeCencProperty(kKeyIdB));

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	// Track change: new version and map, discontinuity, same key
	AppendSegment(chunklist, 1, 2, kSecondMapUri, true);
	// Key rotation: new version and map, no discontinuity, new key
	AppendSegment(chunklist, 2, 3, "init_1_video_key_v3_llhls.m4s");

	auto playlist = chunklist->ToString("", false, false, false);

	auto key_a = ov::String::FormatString("KEYID=0x%s", ov::String(kKeyIdA).UpperCaseString().CStr());
	auto key_b = ov::String::FormatString("KEYID=0x%s", ov::String(kKeyIdB).UpperCaseString().CStr());

	// The unchanged key is advertised once, not repeated for the track change
	auto first_key_a = playlist.IndexOf(key_a.CStr());
	EXPECT_NE(first_key_a, -1);
	EXPECT_EQ(playlist.IndexOf(key_a.CStr(), first_key_a + 1), -1);

	// The rotated key appears before the segment it applies to
	auto key_b_index = playlist.IndexOf(key_b.CStr());
	EXPECT_NE(key_b_index, -1);
	EXPECT_LT(first_key_a, key_b_index);
	EXPECT_LT(key_b_index, playlist.IndexOf("seg_1_2_video_key_llhls.m4s"));

	// Only the track change carries a discontinuity, the rotation does not
	auto discontinuity_index = playlist.IndexOf("#EXT-X-DISCONTINUITY\n");
	EXPECT_NE(discontinuity_index, -1);
	EXPECT_EQ(playlist.IndexOf("#EXT-X-DISCONTINUITY\n", discontinuity_index + 1), -1);
	EXPECT_LT(discontinuity_index, key_b_index);
}

TEST(LLHlsChunklist, DumpedOutputKeepsEveryVersionKey)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());
	// Dumped output lists every segment ever produced
	chunklist->SaveOldSegmentInfo(true);

	// More versions than any fixed retention would keep. A version whose key was dropped
	// while still listed would inherit the previous EXT-X-KEY and fail to decrypt.
	constexpr uint32_t kVersions = 70;
	for (uint32_t version = 1; version <= kVersions; version++)
	{
		auto key_id = ov::String::FormatString("%032x", version);
		chunklist->EnableCenc(version, MakeCencProperty(key_id.CStr()));
		AppendSegment(chunklist, version - 1, version, ov::String::FormatString("init_1_video_key_v%u_llhls.m4s", version));
	}

	// The live window slides past the early segments; they stay listed in the dumped view
	for (uint32_t sequence = 0; sequence + 10 < kVersions; sequence++)
	{
		chunklist->RemoveSegmentInfo(sequence);
	}

	auto playlist = chunklist->ToString("", false, false, false, true, 0);

	for (uint32_t version : {static_cast<uint32_t>(1), kVersions / 2, kVersions})
	{
		auto key_id = ov::String::FormatString("%032x", version).UpperCaseString();
		auto expected = ov::String::FormatString("KEYID=0x%s", key_id.CStr());
		EXPECT_NE(playlist.IndexOf(expected.CStr()), -1) << "missing key of version " << version;
	}
}

TEST(LLHlsChunklist, ClearVersionEndsTheKeyScope)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	chunklist->EnableCenc(1, MakeCencProperty(kKeyIdA));
	// The track changed to a codec CENC cannot encrypt, so this version is produced in
	// the clear. Its segments must not stay covered by the preceding key.
	chunklist->EnableCenc(2, bmff::CencProperty());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 2, kSecondMapUri, true);

	auto playlist = chunklist->ToString("", false, false, false);

	auto key_id = ov::String::FormatString("KEYID=0x%s", ov::String(kKeyIdA).UpperCaseString().CStr());
	auto key_index = playlist.IndexOf(key_id.CStr());
	auto none_index = playlist.IndexOf("#EXT-X-KEY:METHOD=NONE");

	EXPECT_NE(key_index, -1);
	EXPECT_NE(none_index, -1);
	EXPECT_LT(key_index, none_index);

	// The scope ends before the clear segment
	EXPECT_LT(none_index, playlist.IndexOf("seg_1_1_video_key_llhls.m4s"));
}

TEST(LLHlsChunklist, ClearOnlyPlaylistHasNoKeyTag)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	// A stream without DRM registers its versions with a scheme of None. There is no key
	// scope to end, so the playlist carries no key tag at all.
	chunklist->EnableCenc(1, bmff::CencProperty());
	chunklist->EnableCenc(2, bmff::CencProperty());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 2, kSecondMapUri, true);

	auto playlist = chunklist->ToString("", false, false, false);

	EXPECT_EQ(playlist.IndexOf("#EXT-X-KEY"), -1);
}

TEST(LLHlsChunklist, WindowStartingOnClearVersionHasNoKeyTag)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	chunklist->EnableCenc(1, MakeCencProperty(kKeyIdA));
	chunklist->EnableCenc(2, bmff::CencProperty());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 2, kSecondMapUri, true);

	// The protected segment leaves the window, so the key it belonged to is no longer
	// advertised and there is nothing left to end
	chunklist->RemoveSegmentInfo(0);

	auto playlist = chunklist->ToString("", false, false, false);

	EXPECT_EQ(playlist.IndexOf("#EXT-X-KEY"), -1);
}

TEST(LLHlsChunklist, NoKeyTagWhenCencDisabled)
{
	auto chunklist = CreateChunklist(CreateVideoTrack());

	AppendSegment(chunklist, 0, 1, kInitialMapUri);
	AppendSegment(chunklist, 1, 1, kInitialMapUri);

	auto playlist = chunklist->ToString("", false, false, false);

	// No key registered, so the playlist carries no EXT-X-KEY
	EXPECT_EQ(playlist.IndexOf("#EXT-X-KEY:"), -1);
}
