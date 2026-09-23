//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/info/stream.h>
#include <gtest/gtest.h>
#include <publishers/ovt/ovt_stream.h>

#include <map>
#include <vector>

// Pins the rule the index hints rest on: an Edge assigns the group index
// of a track by insertion order per variant (`MediaTrackGroup::AddTrack` uses `_tracks.size()`),
// so an Origin that renumbers `indexHint` for a TrackSet-filtered describe by walking the
// remaining tracks in JSON order (`OvtStream::RenumberIndexHints()`) reproduces exactly the index
// the Edge will compute.

namespace
{
	std::shared_ptr<MediaTrack> MakeTrack(uint32_t id, cmn::MediaType type, const char *variant)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(id);
		track->SetMediaType(type);
		track->SetVariantName(variant);
		return track;
	}

	// Origin: full stream as sent in a non-TrackSet describe (JSON `tracks` order == id order here)
	struct OriginTrack
	{
		uint32_t id;
		cmn::MediaType type;
		const char *variant;
	};

	const std::vector<OriginTrack> ORIGIN_TRACKS = {
		{0, cmn::MediaType::Video, "360p"},
		{1, cmn::MediaType::Video, "720p"},
		{2, cmn::MediaType::Audio, "aac"},	// ko
		{3, cmn::MediaType::Audio, "aac"},	// en
	};

	// Origin-side renumbering through the real implementation: the filtered describe `stream` object with
	// one rendition per remaining track that references it by id; the rewritten hint is read back
	std::map<uint32_t, int> RenumberForEdge(const std::vector<OriginTrack> &tracks, const std::vector<uint32_t> &allowed)
	{
		Json::Value stream;
		stream["tracks"] = Json::Value(Json::arrayValue);
		Json::Value playlist;
		playlist["renditions"] = Json::Value(Json::arrayValue);

		for (const auto &t : tracks)
		{
			bool kept = false;
			for (auto id : allowed)
			{
				kept |= (id == t.id);
			}
			if (kept == false)
			{
				continue;
			}

			Json::Value track;
			track["id"]	  = t.id;
			track["name"] = t.variant;
			stream["tracks"].append(track);

			Json::Value rendition;
			rendition["name"]									   = ov::String::FormatString("r%u", t.id).CStr();
			const bool video									   = (t.type == cmn::MediaType::Video);
			rendition[video ? "videoTrackName" : "audioTrackName"] = t.variant;
			rendition[video ? "videoIndexHint" : "audioIndexHint"] = 99;
			rendition[video ? "videoTrackId" : "audioTrackId"]	   = t.id;
			playlist["renditions"].append(rendition);
		}
		stream["playlists"].append(playlist);

		OvtStream::RenumberIndexHints(stream);

		std::map<uint32_t, int> new_index;
		for (const auto &rendition : stream["playlists"][0]["renditions"])
		{
			if (rendition["videoTrackId"].isUInt())
			{
				new_index[rendition["videoTrackId"].asUInt()] = rendition["videoIndexHint"].asInt();
			}
			else
			{
				new_index[rendition["audioTrackId"].asUInt()] = rendition["audioIndexHint"].asInt();
			}
		}
		return new_index;
	}

	// Edge side: `ReceiveDescribe` adds tracks in JSON order; the group index comes from the stream
	info::Stream BuildEdgeStream(const std::vector<OriginTrack> &tracks, const std::vector<uint32_t> &allowed)
	{
		info::Stream stream(StreamSourceType::Ovt);
		for (const auto &t : tracks)
		{
			bool kept = false;
			for (auto id : allowed)
			{
				kept |= (id == t.id);
			}
			if (kept)
			{
				EXPECT_TRUE(stream.AddTrack(MakeTrack(t.id, t.type, t.variant)));
			}
		}
		return stream;
	}
}  // namespace

// Full stream: the Edge's group index equals the Origin's original `indexHint`
TEST(TrackSetIndexHint, FullStreamIndicesMatchInsertionOrder)
{
	auto stream = BuildEdgeStream(ORIGIN_TRACKS, {0, 1, 2, 3});

	auto aac	= stream.GetMediaTrackGroup("aac");
	ASSERT_NE(aac, nullptr);
	ASSERT_EQ(aac->GetTrackCount(), 2u);
	EXPECT_EQ(aac->GetTrack(0)->GetId(), 2u);  // ko
	EXPECT_EQ(aac->GetTrack(1)->GetId(), 3u);  // en
	EXPECT_EQ(stream.GetTrack(3)->GetGroupIndex(), 1);
}

// TrackSet {0, 3} with the original hint (audio aac, 1) delivered as-is picks nothing
TEST(TrackSetIndexHint, OriginalHintBreaksAfterFilter)
{
	auto stream = BuildEdgeStream(ORIGIN_TRACKS, {0, 3});

	auto aac	= stream.GetMediaTrackGroup("aac");
	ASSERT_NE(aac, nullptr);
	ASSERT_EQ(aac->GetTrackCount(), 1u);
	EXPECT_EQ(aac->GetTrack(1), nullptr);	   // HLS: "audio track index 1 ... not found", rendition ignored
	EXPECT_EQ(aac->GetTrack(0)->GetId(), 3u);  // the track that should have been picked
}

// The Origin's renumbering reproduces the Edge's index for every remaining track
TEST(TrackSetIndexHint, RenumberedHintMatchesEdgeIndex)
{
	const std::vector<std::vector<uint32_t>> track_sets = {
		{0, 3},		// 360p + en
		{1, 2},		// 720p + ko
		{3},		// en only
		{0, 1, 3},	// both videos + en
		{2, 3},		// both audios
	};

	for (const auto &allowed : track_sets)
	{
		auto new_index = RenumberForEdge(ORIGIN_TRACKS, allowed);
		auto stream	   = BuildEdgeStream(ORIGIN_TRACKS, allowed);

		for (auto id : allowed)
		{
			auto track = stream.GetTrack(id);
			ASSERT_NE(track, nullptr);
			EXPECT_EQ(track->GetGroupIndex(), new_index[id]) << "track " << id;

			// HLS resolves (variant, `indexHint`) through the group
			auto group = stream.GetMediaTrackGroup(track->GetVariantName());
			ASSERT_NE(group, nullptr);
			auto picked = group->GetTrack(new_index[id]);
			ASSERT_NE(picked, nullptr) << "track " << id;
			EXPECT_EQ(picked->GetId(), id);
		}
	}
}
