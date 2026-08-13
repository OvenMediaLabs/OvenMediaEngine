//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/info/media_track.h>

#include "rtc_playlist.h"

namespace
{
	using cmn::MediaCodecId;
	using cmn::MediaType;

	std::shared_ptr<const MediaTrack> MakeTrack(MediaType type, MediaCodecId codec, int32_t bitrate)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetMediaType(type);
		track->SetCodecId(codec);
		track->SetBitrateByConfig(bitrate);
		return track;
	}

	// v == None -> no video track; a == None -> no audio track.
	std::shared_ptr<const RtcRendition> MakeRendition(const ov::String &name,
													  MediaCodecId v, int32_t v_bitrate,
													  MediaCodecId a, int32_t a_bitrate)
	{
		std::shared_ptr<const MediaTrack> vt = (v == MediaCodecId::None) ? nullptr : MakeTrack(MediaType::Video, v, v_bitrate);
		std::shared_ptr<const MediaTrack> at = (a == MediaCodecId::None) ? nullptr : MakeTrack(MediaType::Audio, a, a_bitrate);
		return std::make_shared<RtcRendition>(name, vt, at);
	}

	bool HasRendition(const std::shared_ptr<const RtcPlaylist> &pl, const char *name)
	{
		return (pl != nullptr) && (pl->GetRendition(name) != nullptr);
	}
}  // namespace

// Audio-only rendition declared AFTER the video rendition is cross-added into the
// video playlist; the video rendition remains the default (GetFirstRendition).
TEST(RtcMasterPlaylist, CrossAddVideoThenAudio)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("video", MediaCodecId::H264, 2000000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 48000));

	auto vpl = master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Opus);
	ASSERT_NE(vpl, nullptr);
	EXPECT_TRUE(HasRendition(vpl, "video"));
	EXPECT_TRUE(HasRendition(vpl, "audio"));  // cross-added
	ASSERT_NE(vpl->GetFirstRendition(), nullptr);
	EXPECT_STREQ(vpl->GetFirstRendition()->GetName().CStr(), "video");
	EXPECT_NE(vpl->GetFirstRendition()->GetVideoTrack(), nullptr);
}

// Audio-only rendition declared BEFORE the video rendition is back-filled into the
// later-created video playlist; the video rendition (not the earlier audio-only one)
// must remain the playlist default regardless of declaration order.
TEST(RtcMasterPlaylist, BackFillAudioThenVideo)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 48000));
	master.AddRendition(MakeRendition("video", MediaCodecId::H264, 2000000, MediaCodecId::Opus, 128000));

	auto vpl = master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Opus);
	ASSERT_NE(vpl, nullptr);
	EXPECT_TRUE(HasRendition(vpl, "video"));
	EXPECT_TRUE(HasRendition(vpl, "audio"));  // back-filled
	ASSERT_NE(vpl->GetFirstRendition(), nullptr);
	EXPECT_STREQ(vpl->GetFirstRendition()->GetName().CStr(), "video");
	EXPECT_NE(vpl->GetFirstRendition()->GetVideoTrack(), nullptr);
}

// In a video playlist, automatic bitrate ABR never selects the audio-only rendition,
// even though it is the lowest bitrate; it stays reachable only by name.
TEST(RtcPlaylist, VideoPlaylistAbrExcludesAudioOnly)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("high", MediaCodecId::H264, 2000000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("low", MediaCodecId::H264, 300000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 48000));

	auto vpl = master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Opus);
	ASSERT_NE(vpl, nullptr);
	auto high = vpl->GetRendition("high");
	auto low = vpl->GetRendition("low");
	auto audio = vpl->GetRendition("audio");
	ASSERT_NE(high, nullptr);
	ASSERT_NE(low, nullptr);
	ASSERT_NE(audio, nullptr);

	// audio-only is the lowest bitrate but must be skipped: no lower rendition than low video
	EXPECT_EQ(vpl->GetNextLowerBitrateRendition(low), nullptr);
	// from high, the next lower is the low *video* rendition
	EXPECT_EQ(vpl->GetNextLowerBitrateRendition(high), low);
	// from low, the next higher is the high video rendition
	EXPECT_EQ(vpl->GetNextHigherBitrateRendition(low), high);
	// audio-only is never an ABR destination
	EXPECT_NE(vpl->GetNextLowerBitrateRendition(high), audio);
}

// The audio-only rendition's bitrate is placed BETWEEN the two video renditions, so without
// the guard it would be the closest ABR neighbour in BOTH directions (next-lower from the high
// rendition, next-higher from the low rendition). The guard must skip it either way, so ABR
// steps between the video renditions and never onto audio-only. Every assertion here flips if
// the guard is removed, unlike the lowest-rung case above.
TEST(RtcPlaylist, VideoPlaylistAbrSkipsInterstitialAudioOnly)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("vhigh", MediaCodecId::H264, 2000000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("vlow", MediaCodecId::H264, 300000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 800000));

	auto vpl = master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Opus);
	ASSERT_NE(vpl, nullptr);
	auto vhigh = vpl->GetRendition("vhigh");
	auto vlow = vpl->GetRendition("vlow");
	auto audio = vpl->GetRendition("audio");
	ASSERT_NE(vhigh, nullptr);
	ASSERT_NE(vlow, nullptr);
	ASSERT_NE(audio, nullptr);
	// precondition: audio-only really is the closest neighbour by bitrate in both directions
	ASSERT_GT(audio->GetBitrates(), vlow->GetBitrates());
	ASSERT_LT(audio->GetBitrates(), vhigh->GetBitrates());

	// down from vhigh: without the guard the closest lower is audio; with the guard, vlow
	EXPECT_EQ(vpl->GetNextLowerBitrateRendition(vhigh), vlow);
	EXPECT_NE(vpl->GetNextLowerBitrateRendition(vhigh), audio);
	// up from vlow: without the guard the closest higher is audio; with the guard, vhigh
	EXPECT_EQ(vpl->GetNextHigherBitrateRendition(vlow), vhigh);
	EXPECT_NE(vpl->GetNextHigherBitrateRendition(vlow), audio);
}

// A video session that explicitly switched to the audio-only rendition stays there:
// automatic ABR must not move it in either direction (so it does not auto-resume video when
// bandwidth recovers, even if the client left auto-ABR enabled). The audio-only bitrate is
// placed between the two video renditions so both directions would otherwise pick a video
// rendition; the pin must return null both ways.
TEST(RtcPlaylist, VideoPlaylistAbrPinsAudioOnlyBase)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("vhigh", MediaCodecId::H264, 2000000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("vlow", MediaCodecId::H264, 300000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 800000));

	auto vpl = master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Opus);
	ASSERT_NE(vpl, nullptr);
	auto audio = vpl->GetRendition("audio");
	ASSERT_NE(audio, nullptr);
	EXPECT_EQ(vpl->GetNextHigherBitrateRendition(audio), nullptr);
	EXPECT_EQ(vpl->GetNextLowerBitrateRendition(audio), nullptr);
}

// Regression guard: in an audio-only playlist (an audio-only subscriber's bucket),
// bitrate ABR still works normally among the Opus renditions.
TEST(RtcPlaylist, AudioOnlyPlaylistAbrWorks)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("a_hi", MediaCodecId::None, 0, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("a_mid", MediaCodecId::None, 0, MediaCodecId::Opus, 64000));
	master.AddRendition(MakeRendition("a_lo", MediaCodecId::None, 0, MediaCodecId::Opus, 32000));

	auto apl = master.GetPlaylist(MediaCodecId::None, MediaCodecId::Opus);
	ASSERT_NE(apl, nullptr);
	auto hi = apl->GetRendition("a_hi");
	auto mid = apl->GetRendition("a_mid");
	auto lo = apl->GetRendition("a_lo");
	ASSERT_NE(hi, nullptr);
	ASSERT_NE(mid, nullptr);
	ASSERT_NE(lo, nullptr);

	EXPECT_EQ(apl->GetNextLowerBitrateRendition(hi), mid);
	EXPECT_EQ(apl->GetNextLowerBitrateRendition(mid), lo);
	EXPECT_EQ(apl->GetNextLowerBitrateRendition(lo), nullptr);
	EXPECT_EQ(apl->GetNextHigherBitrateRendition(lo), mid);
}

// A stream with only video renditions: nothing is cross-added, normal video ABR holds.
TEST(RtcMasterPlaylist, NoAudioOnlyNoCrossAdd)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("high", MediaCodecId::H264, 2000000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("low", MediaCodecId::H264, 300000, MediaCodecId::Opus, 128000));

	auto vpl = master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Opus);
	ASSERT_NE(vpl, nullptr);
	EXPECT_EQ(vpl->GetRenditions().size(), 2u);
	EXPECT_EQ(vpl->GetNextLowerBitrateRendition(vpl->GetRendition("high")), vpl->GetRendition("low"));
}

// An audio-only rendition is cross-added only into video playlists that share its
// audio codec: an Opus audio-only rendition is not added to an AAC video playlist.
TEST(RtcMasterPlaylist, MismatchedAudioCodecNoCrossAdd)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("video", MediaCodecId::H264, 2000000, MediaCodecId::Aac, 128000));
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 48000));

	auto vpl = master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Aac);
	ASSERT_NE(vpl, nullptr);
	EXPECT_TRUE(HasRendition(vpl, "video"));
	EXPECT_FALSE(HasRendition(vpl, "audio"));
}

// An audio-only rendition is cross-added into EVERY video playlist that shares its audio codec.
TEST(RtcMasterPlaylist, CrossAddToMultipleVideoPlaylists)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("h264", MediaCodecId::H264, 2000000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("vp8", MediaCodecId::Vp8, 2000000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 48000));

	EXPECT_TRUE(HasRendition(master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Opus), "audio"));
	EXPECT_TRUE(HasRendition(master.GetPlaylist(MediaCodecId::Vp8, MediaCodecId::Opus), "audio"));
}

// Video-only renditions (no audio track): no crash, their own (video,None) bucket, ABR among
// them works, and an Opus audio-only rendition is not injected into the audio-less video playlist.
TEST(RtcMasterPlaylist, VideoOnlyRenditionsNoAudioTrack)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(true);
	master.AddRendition(MakeRendition("v_hi", MediaCodecId::H264, 2000000, MediaCodecId::None, 0));
	master.AddRendition(MakeRendition("v_lo", MediaCodecId::H264, 300000, MediaCodecId::None, 0));
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 48000));

	auto vpl = master.GetPlaylist(MediaCodecId::H264, MediaCodecId::None);
	ASSERT_NE(vpl, nullptr);
	EXPECT_FALSE(HasRendition(vpl, "audio"));  // audio codec None != Opus -> not cross-added
	EXPECT_EQ(vpl->GetNextLowerBitrateRendition(vpl->GetRendition("v_hi")), vpl->GetRendition("v_lo"));
}

// With the fallback disabled (the default), an audio-only rendition is NOT cross-added into a
// video playlist; it stays only in its own (None, audio) playlist.
TEST(RtcMasterPlaylist, AudioOnlyFallbackDisabledNoCrossAdd)
{
	RtcMasterPlaylist master("m", "m");
	master.SetWebRtcAudioOnlyFallback(false);  // explicit; this is the default
	master.AddRendition(MakeRendition("video", MediaCodecId::H264, 2000000, MediaCodecId::Opus, 128000));
	master.AddRendition(MakeRendition("audio", MediaCodecId::None, 0, MediaCodecId::Opus, 48000));

	// not exposed to a video session...
	EXPECT_FALSE(HasRendition(master.GetPlaylist(MediaCodecId::H264, MediaCodecId::Opus), "audio"));
	// ...but still available in its own audio-only playlist
	EXPECT_TRUE(HasRendition(master.GetPlaylist(MediaCodecId::None, MediaCodecId::Opus), "audio"));
}
