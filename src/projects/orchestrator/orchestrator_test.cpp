//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/info/playlist_file.h>
#include <base/ovlibrary/url.h>
#include <gtest/gtest.h>


namespace
{
	std::shared_ptr<const ov::Url> ParseUrl(const char *url)
	{
		return ov::Url::Parse(url);
	}
}  // namespace

TEST(OrchestratorPlaylistScope, MasterPlaylistAllowList)
{
	// Master playlists qualify (HLS .m3u8 master, DASH .mpd).
	EXPECT_TRUE(info::IsMasterPlaylistFileName("playlist-main.m3u8"));
	EXPECT_TRUE(info::IsMasterPlaylistFileName("master.m3u8"));
	EXPECT_TRUE(info::IsMasterPlaylistFileName("manifest.mpd"));

	// HLS sub-playlists do not qualify.
	EXPECT_FALSE(info::IsMasterPlaylistFileName("chunklist_b900000.m3u8"));
	EXPECT_FALSE(info::IsMasterPlaylistFileName("medialist_a1.m3u8"));

	// Segments / artifacts / unknown extensions never qualify.
	EXPECT_FALSE(info::IsMasterPlaylistFileName("thumb.jpg"));
	EXPECT_FALSE(info::IsMasterPlaylistFileName("thumb.webp"));
	EXPECT_FALSE(info::IsMasterPlaylistFileName("segment_00001.ts"));
	EXPECT_FALSE(info::IsMasterPlaylistFileName("init.mp4"));
	EXPECT_FALSE(info::IsMasterPlaylistFileName("master"));	 // no extension
	EXPECT_FALSE(info::IsMasterPlaylistFileName(""));
}

TEST(OrchestratorPlaylistScope, ResolvePlaylistScopeUsesOnlyMasterPlaylistFiles)
{
	EXPECT_TRUE(info::GetMasterPlaylistFileName(nullptr).IsEmpty());
	// OVT URLs are trusted verbatim - orchestrator only appends master playlist names there,
	// so whatever file segment is present is honoured.
	EXPECT_EQ(info::GetMasterPlaylistFileName(ParseUrl("ovt://origin/app/stream/master.m3u8")), "master.m3u8");
	EXPECT_EQ(info::GetMasterPlaylistFileName(ParseUrl("ovt://origin/app/stream/manifest.mpd")), "manifest.mpd");
	// HTTP triggers: only master playlist files become a playlist scope.
	EXPECT_EQ(info::GetMasterPlaylistFileName(ParseUrl("http://origin/app/stream/playlist.m3u8")), "playlist.m3u8");
	EXPECT_EQ(info::GetMasterPlaylistFileName(ParseUrl("http://origin/app/stream/manifest.mpd")), "manifest.mpd");
	// Sub-playlists / segments / thumbnails / unknown extensions stay full-stream.
	EXPECT_TRUE(info::GetMasterPlaylistFileName(ParseUrl("http://origin/app/stream/medialist.m3u8")).IsEmpty());
	EXPECT_TRUE(info::GetMasterPlaylistFileName(ParseUrl("http://origin/app/stream/chunklist_b1.m3u8")).IsEmpty());
	EXPECT_TRUE(info::GetMasterPlaylistFileName(ParseUrl("http://origin/app/stream/thumb.jpg")).IsEmpty());
	EXPECT_TRUE(info::GetMasterPlaylistFileName(ParseUrl("http://origin/app/stream/mobile")).IsEmpty());  // no extension
}

TEST(OrchestratorPlaylistScope, AppendPlaylistScopeOnlyTouchesOvtUrls)
{
	std::vector<ov::String> url_list = {
		"ovt://origin/app/stream",
		"ovt://backup/app/stream/mobile.m3u8",
		"rtsp://origin/app/stream",
	};

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/mobile.m3u8"), url_list);

	// Extension is stripped: OVT URLs carry the playlist name, not the filename.
	EXPECT_EQ(url_list[0], "ovt://origin/app/stream/mobile");
	EXPECT_EQ(url_list[1], "ovt://backup/app/stream/mobile");
	EXPECT_EQ(url_list[2], "rtsp://origin/app/stream");
}

TEST(OrchestratorPlaylistScope, AppendPlaylistScopeSkipsArtifactRequests)
{
	std::vector<ov::String> url_list = {
		"ovt://origin/app/stream",
		"ovt://backup/app/stream",
	};

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/medialist.m3u8"), url_list);

	EXPECT_EQ(url_list[0], "ovt://origin/app/stream");
	EXPECT_EQ(url_list[1], "ovt://backup/app/stream");
}

TEST(OrchestratorPlaylistScope, AppendPlaylistScopeSkipsThumbnailAndSegmentRequests)
{
	std::vector<ov::String> url_list = {
		"ovt://origin/app/stream",
	};

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/thumb.jpg"), url_list);
	EXPECT_EQ(url_list[0], "ovt://origin/app/stream");

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/seg_00001.ts"), url_list);
	EXPECT_EQ(url_list[0], "ovt://origin/app/stream");
}

TEST(OrchestratorPlaylistScope, AppendPlaylistScopeSupportsDashManifest)
{
	std::vector<ov::String> url_list = {
		"ovt://origin/app/stream",
	};

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/manifest.mpd"), url_list);
	EXPECT_EQ(url_list[0], "ovt://origin/app/stream/manifest");
}

TEST(OrchestratorPlaylistScope, AppendPlaylistScopeReplacesDifferentExistingFileSegment)
{
	// NEW-3 regression: an OVT URL configured with a fixed playlist scope (e.g. an admin
	// origin map entry like "ovt://host/app/stream/master.m3u8") used to be *concatenated*
	// with the incoming trigger's playlist name -- producing a malformed URL like
	// "/app/stream/master.m3u8/mobile.m3u8" that the origin would reject and force a
	// permanent compatibility-mode fallback. The fix replaces the file segment in place.
	// Extension is also stripped so the playlist name matches the origin's _playlists key.
	std::vector<ov::String> url_list = {
		"ovt://origin/app/stream/master.m3u8",
		"ovt://backup/app/stream",
	};

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/mobile.m3u8"), url_list);

	EXPECT_EQ(url_list[0], "ovt://origin/app/stream/mobile");
	EXPECT_EQ(url_list[1], "ovt://backup/app/stream/mobile");
}

TEST(OrchestratorPlaylistScope, AppendPlaylistScopeIsIdempotentWhenScopeAlreadyMatches)
{
	// An OVT URL with an extension-bearing file segment is normalised to the bare name on
	// the first call; a second call with the same trigger must then be a no-op.
	std::vector<ov::String> url_list = {
		"ovt://origin/app/stream/mobile.m3u8?token=abc",
	};

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/mobile.m3u8"), url_list);
	EXPECT_EQ(url_list[0], "ovt://origin/app/stream/mobile?token=abc");

	// Already correctly scoped (name without extension): applying again must be a no-op.
	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/mobile.m3u8"), url_list);
	EXPECT_EQ(url_list[0], "ovt://origin/app/stream/mobile?token=abc");
}

TEST(OrchestratorPlaylistScope, AppendPlaylistScopePreservesMultiParamQueryAcrossReplace)
{
	// NEW-3 follow-up: SetFile-based replacement must round-trip multiple query parameters
	// even when the URL had a different existing file segment. (We exercise both the
	// "replace different file" path and the "skip identical" path under the same query
	// string to lock down ov::Url's path/query handling.)
	std::vector<ov::String> url_list = {
		"ovt://origin/app/stream/master.m3u8?token=abc&region=apse1&debug=1",
	};

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/mobile.m3u8"), url_list);
	EXPECT_EQ(url_list[0], "ovt://origin/app/stream/mobile?token=abc&region=apse1&debug=1");

	// Idempotent on the now-resolved URL.
	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/mobile.m3u8"), url_list);
	EXPECT_EQ(url_list[0], "ovt://origin/app/stream/mobile?token=abc&region=apse1&debug=1");
}

TEST(OrchestratorPlaylistScope, AppendPlaylistScopeAttachesScopeToBareOvtUrl)
{
	// SetFile() handles the "no file segment" OVT URL form by extending the path
	// component array as needed. Verify that bare app/stream URLs are scoped, and that
	// query parameters survive the re-emission.
	std::vector<ov::String> url_list = {
		"ovt://origin:9000/app/stream?failover=primary",
	};

	info::AppendPlaylistScopeToOvtUrls(ParseUrl("http://trigger/app/stream/mobile.m3u8"), url_list);
	EXPECT_EQ(url_list[0], "ovt://origin:9000/app/stream/mobile?failover=primary");
}
