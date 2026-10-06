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

// Rendition track ids: how a stream fills them from its groups,
// and how a consumer confirms them against the tracks it registered

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

	// 720p, 360p, aac ko, aac en
	info::Stream BuildStream()
	{
		info::Stream stream(StreamSourceType::Ovt);
		EXPECT_TRUE(stream.AddTrack(MakeTrack(0, cmn::MediaType::Video, "720p")));
		EXPECT_TRUE(stream.AddTrack(MakeTrack(1, cmn::MediaType::Video, "360p")));
		EXPECT_TRUE(stream.AddTrack(MakeTrack(2, cmn::MediaType::Audio, "aac")));
		EXPECT_TRUE(stream.AddTrack(MakeTrack(3, cmn::MediaType::Audio, "aac")));
		return stream;
	}
}  // namespace

// Video resolves for any hint (-1 means group 0); audio only for a hint >= 0
TEST(RenditionTrackId, ResolveFollowsTheSideRules)
{
	auto stream = BuildStream();
	auto playlist = std::make_shared<info::Playlist>("P", "p", false);

	auto video_default = std::make_shared<info::Rendition>("v", "720p", "aac");
	video_default->SetVideoIndexHint(-1);
	video_default->SetAudioIndexHint(-1);
	playlist->AddRendition(video_default);

	auto en = std::make_shared<info::Rendition>("en", "360p", "aac");
	en->SetVideoIndexHint(0);
	en->SetAudioIndexHint(1);
	playlist->AddRendition(en);

	stream.ResolveRenditionTrackIds(playlist);

	EXPECT_EQ(video_default->GetVideoTrackId(), 0u);
	EXPECT_FALSE(video_default->GetAudioTrackId().has_value());
	EXPECT_EQ(en->GetVideoTrackId(), 1u);
	EXPECT_EQ(en->GetAudioTrackId(), 3u);
}

// A side that cannot resolve stays empty and nothing else is disturbed
TEST(RenditionTrackId, UnresolvableSidesStayEmpty)
{
	auto stream = BuildStream();
	auto playlist = std::make_shared<info::Playlist>("P", "p", false);

	auto missing_variant = std::make_shared<info::Rendition>("a", "1080p", "aac");
	missing_variant->SetAudioIndexHint(0);
	playlist->AddRendition(missing_variant);

	auto out_of_range = std::make_shared<info::Rendition>("b", "720p", "aac");
	out_of_range->SetAudioIndexHint(5);
	playlist->AddRendition(out_of_range);

	auto wrong_type = std::make_shared<info::Rendition>("c", "aac", "720p");  // sides swapped
	wrong_type->SetAudioIndexHint(0);
	playlist->AddRendition(wrong_type);

	auto empty_names = std::make_shared<info::Rendition>("d", "", "");
	playlist->AddRendition(empty_names);

	stream.ResolveRenditionTrackIds(playlist);

	EXPECT_FALSE(missing_variant->GetVideoTrackId().has_value());
	EXPECT_EQ(missing_variant->GetAudioTrackId(), 2u);
	EXPECT_EQ(out_of_range->GetVideoTrackId(), 0u);
	EXPECT_FALSE(out_of_range->GetAudioTrackId().has_value());
	EXPECT_FALSE(wrong_type->GetVideoTrackId().has_value());
	EXPECT_FALSE(wrong_type->GetAudioTrackId().has_value());
	EXPECT_FALSE(empty_names->GetVideoTrackId().has_value());
	EXPECT_FALSE(empty_names->GetAudioTrackId().has_value());

	stream.ResolveRenditionTrackIds(nullptr);  // tolerated
}

// The 3-way confirmation: id exists, same variant, right media type; anything else falls back
TEST(RenditionTrackId, ConfirmationRules)
{
	auto stream = BuildStream();
	bool id_unconfirmed = true;

	info::Rendition no_id("r", "720p", "aac");
	EXPECT_EQ(stream.GetRenditionTrack(no_id, cmn::MediaType::Video, &id_unconfirmed), nullptr);
	EXPECT_FALSE(id_unconfirmed);

	info::Rendition ok("r", "360p", "aac");
	ok.SetVideoTrackId(1);
	ok.SetAudioTrackId(3);
	auto video = stream.GetRenditionTrack(ok, cmn::MediaType::Video, &id_unconfirmed);
	ASSERT_TRUE(video != nullptr);
	EXPECT_EQ(video->GetId(), 1u);
	EXPECT_FALSE(id_unconfirmed);
	auto audio = stream.GetRenditionTrack(ok, cmn::MediaType::Audio, &id_unconfirmed);
	ASSERT_TRUE(audio != nullptr);
	EXPECT_EQ(audio->GetId(), 3u);

	info::Rendition wrong_variant("r", "720p", "aac");
	wrong_variant.SetVideoTrackId(1);  // track 1 is 360p
	EXPECT_EQ(stream.GetRenditionTrack(wrong_variant, cmn::MediaType::Video, &id_unconfirmed), nullptr);
	EXPECT_TRUE(id_unconfirmed);

	info::Rendition wrong_type("r", "aac", "aac");
	wrong_type.SetVideoTrackId(2);  // track 2 is audio
	EXPECT_EQ(stream.GetRenditionTrack(wrong_type, cmn::MediaType::Video, &id_unconfirmed), nullptr);
	EXPECT_TRUE(id_unconfirmed);

	info::Rendition missing("r", "720p", "aac");
	missing.SetVideoTrackId(99);
	EXPECT_EQ(stream.GetRenditionTrack(missing, cmn::MediaType::Video, &id_unconfirmed), nullptr);
	EXPECT_TRUE(id_unconfirmed);

	// The out-pointer is optional
	EXPECT_EQ(stream.GetRenditionTrack(missing, cmn::MediaType::Video), nullptr);
}

// A failover to an origin whose id space differs: the id points at a track of another variant and falls back,
// while an id that happens to match variant and type is accepted (the documented limit of the check)
TEST(RenditionTrackId, ForeignIdSpace)
{
	info::Stream other(StreamSourceType::Ovt);
	other.AddTrack(MakeTrack(3, cmn::MediaType::Video, "360p"));
	other.AddTrack(MakeTrack(1, cmn::MediaType::Audio, "aac"));

	info::Rendition rendition("r", "360p", "aac");
	rendition.SetVideoTrackId(1);
	rendition.SetAudioTrackId(3);

	bool id_unconfirmed = false;
	EXPECT_EQ(other.GetRenditionTrack(rendition, cmn::MediaType::Video, &id_unconfirmed), nullptr);
	EXPECT_TRUE(id_unconfirmed);
	EXPECT_EQ(other.GetRenditionTrack(rendition, cmn::MediaType::Audio, &id_unconfirmed), nullptr);
	EXPECT_TRUE(id_unconfirmed);
}

TEST(RenditionTrackId, PlaylistReplacement)
{
	auto stream = BuildStream();
	EXPECT_TRUE(stream.AddPlaylist(std::make_shared<info::Playlist>("A", "a", false)));
	EXPECT_FALSE(stream.AddPlaylist(std::make_shared<info::Playlist>("A2", "a", false)));  // same file name is refused
	EXPECT_EQ(stream.GetPlaylists().size(), 1u);
	EXPECT_STREQ(stream.GetPlaylist("a")->GetName().CStr(), "A");

	stream.ClearPlaylists();
	EXPECT_TRUE(stream.GetPlaylists().empty());
	EXPECT_TRUE(stream.AddPlaylist(std::make_shared<info::Playlist>("A2", "a", false)));
	EXPECT_STREQ(stream.GetPlaylist("a")->GetName().CStr(), "A2");
}

// Copies deep-copy the renditions with their track ids, and equality sees the ids
TEST(RenditionTrackId, PlaylistCopyAndEquality)
{
	info::Playlist playlist("P", "p", false);
	auto rendition = std::make_shared<info::Rendition>("r", "v", "a");
	rendition->SetVideoTrackId(10);
	rendition->SetAudioTrackId(20);
	playlist.AddRendition(rendition);

	info::Playlist copy(playlist);
	ASSERT_EQ(copy.GetRenditionList().size(), 1u);
	EXPECT_EQ(copy.GetRenditionList()[0]->GetVideoTrackId(), 10u);
	EXPECT_EQ(copy.GetRenditionList()[0]->GetAudioTrackId(), 20u);
	EXPECT_NE(copy.GetRenditionList()[0].get(), rendition.get());  // deep copy
	EXPECT_TRUE(copy == playlist);

	copy.GetRenditionList()[0]->SetAudioTrackId(21);
	EXPECT_TRUE(copy != playlist);

	info::Rendition a("r", "v", "a");
	info::Rendition b("r", "v", "a");
	EXPECT_TRUE(a == b);
	b.SetVideoTrackId(0);
	EXPECT_TRUE(a != b);
}
