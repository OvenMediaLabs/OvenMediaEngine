//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/info/media_track.h>
#include <base/info/playlist.h>
#include <base/ovlibrary/json.h>
#include <gtest/gtest.h>
#include <modules/ovt_packetizer/ovt_wire.h>

#include <map>
#include <vector>

#include "ovt_stream.h"

// DESCRIBE wire form of the OVT publisher.
//
// The describe an OVT1 edge receives is not byte-identical to `v0.21.0.0`: it carries the OVT2
// keys, which every release from v0.15.0 on ignores because they all read the response by key name.
// What is pinned here is that those releases still read it:
//
// 1. Reverse test: the output read with the parser rules of each release representative.
//    The structural rules were identical from `v0.15.8.1` to `v0.21.0.0`;
//    what differs is how each release numbers the enums,
//    so the tables below record the numbering of every representative,
//    and the expectations pin both the releases that read the output correctly
//    and the ones that already misread it.
// 2. Per-key expectations on the values `v0.21.0.0` decides on, so a change to one of them fails here
//    rather than only on the wire.

namespace
{
	std::shared_ptr<MediaTrack> MakeVideoTrack()
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(1);
		track->SetCodecId(cmn::MediaCodecId::H264);
		track->SetMediaType(cmn::MediaType::Video);
		track->SetVariantName("pt_video");
		track->SetPublicName("1080p");
		track->SetTimeBase(1, 90000);
		track->SetBitrateByConfig(5000000);
		track->SetFrameRateByConfig(30.0);
		track->SetMaxFrameRate(30.0);
		track->SetResolution(1920, 1080);
		track->SetMaxResolution(1920, 1080);
		return track;
	}

	std::shared_ptr<MediaTrack> MakeAudioTrack()
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(2);
		track->SetCodecId(cmn::MediaCodecId::Aac);
		track->SetMediaType(cmn::MediaType::Audio);
		track->SetVariantName("pt_audio");
		track->SetPublicName("default");
		track->SetTimeBase(1, 48000);
		track->SetBitrateByConfig(128000);
		track->SetSampleRate(48000);
		track->SetSampleFormat(cmn::AudioSample::Format::FltP);
		track->SetChannelLayout(cmn::AudioChannel::Layout::LayoutStereo);
		return track;
	}

	Json::Value BuildFixtureDescription()
	{
		auto playlist = std::make_shared<info::Playlist>("ABR", "abr", false);
		playlist->SetWebRtcAutoAbr(true);
		playlist->SetHlsChunklistPathDepth(-1);
		playlist->EnableTsPackaging(false);

		auto rendition = std::make_shared<info::Rendition>("1080p", "pt_video", "pt_audio");
		rendition->SetVideoIndexHint(0);
		rendition->SetAudioIndexHint(0);
		playlist->AddRendition(rendition);

		std::map<ov::String, std::shared_ptr<const info::Playlist>> playlists = {{"abr", playlist}};
		std::map<int32_t, std::shared_ptr<const MediaTrack>> tracks			  = {{1, MakeVideoTrack()}, {2, MakeAudioTrack()}};

		return OvtStream::BuildDescription("app", "stream", "origin-uuid", playlists, tracks);
	}

	// Enum numbering of each release representative, extracted from the tag sources
	// (`release-wire-data.md`, `extract_release_wire_data.py`).
	// Keys are the values a release put on the wire.
	struct ReleaseTable
	{
		const char *tag;
		std::map<int, const char *> codec_id;
		std::map<int, const char *> bitstream_format;
	};

	const std::vector<ReleaseTable> RELEASES = {
		{"v0.15.8.1",
		 {{0, "None"}, {1, "H264"}, {2, "H265"}, {3, "Vp8"}, {4, "Vp9"}, {5, "Flv"}, {6, "Aac"}, {7, "Mp3"}, {8, "Opus"}, {9, "Jpeg"}, {10, "Png"}},
		 {{-1, "Unknown"}, {0, "H264_AVCC"}, {1, "H264_ANNEXB"}, {2, "H264_RTP_RFC_6184"}, {3, "H265_ANNEXB"}, {4, "VP8"}, {5, "VP8_RTP_RFC_7741"}, {6, "AAC_RAW"}, {7, "AAC_MPEG4_GENERIC"}, {8, "AAC_ADTS"}, {9, "AAC_LATM"}, {10, "OPUS"}, {11, "OPUS_RTP_RFC_7587"}, {12, "JPEG"}, {13, "PNG"}, {14, "ID3v2"}}},
		{"v0.15.12",
		 {{0, "None"}, {1, "H264"}, {2, "H265"}, {3, "Vp8"}, {4, "Vp9"}, {5, "Flv"}, {6, "Aac"}, {7, "Mp3"}, {8, "Opus"}, {9, "Jpeg"}, {10, "Png"}},
		 {{-1, "Unknown"}, {0, "H264_AVCC"}, {1, "H264_ANNEXB"}, {2, "H264_RTP_RFC_6184"}, {3, "H265_ANNEXB"}, {4, "VP8"}, {5, "VP8_RTP_RFC_7741"}, {6, "AAC_RAW"}, {7, "AAC_MPEG4_GENERIC"}, {8, "AAC_ADTS"}, {9, "AAC_LATM"}, {10, "OPUS"}, {11, "OPUS_RTP_RFC_7587"}, {12, "JPEG"}, {13, "PNG"}, {14, "ID3v2"}}},
		{"v0.17.3.1",
		 {{0, "None"}, {1, "H264"}, {2, "H265"}, {3, "Vp8"}, {4, "Vp9"}, {5, "Flv"}, {6, "Aac"}, {7, "Mp3"}, {8, "Opus"}, {9, "Jpeg"}, {10, "Png"}, {11, "Webp"}},
		 {{-1, "Unknown"}, {0, "H264_AVCC"}, {1, "H264_ANNEXB"}, {2, "H264_RTP_RFC_6184"}, {3, "H265_ANNEXB"}, {4, "VP8"}, {5, "VP8_RTP_RFC_7741"}, {6, "AAC_RAW"}, {7, "AAC_MPEG4_GENERIC"}, {8, "AAC_ADTS"}, {9, "AAC_LATM"}, {10, "OPUS"}, {11, "OPUS_RTP_RFC_7587"}, {12, "JPEG"}, {13, "PNG"}, {14, "WEBP"}, {15, "ID3v2"}, {16, "HVCC"}, {17, "MP3"}, {18, "OVEN_EVENT"}, {19, "CUE"}, {20, "AMF"}}},
		{"v0.18.0",
		 {{0, "None"}, {1, "H264"}, {2, "H265"}, {3, "Vp8"}, {4, "Vp9"}, {5, "Flv"}, {6, "Aac"}, {7, "Mp3"}, {8, "Opus"}, {9, "Jpeg"}, {10, "Png"}, {11, "Webp"}},
		 {{-1, "Unknown"}, {0, "H264_AVCC"}, {1, "H264_ANNEXB"}, {2, "H264_RTP_RFC_6184"}, {3, "HVCC"}, {4, "H265_ANNEXB"}, {5, "VP8"}, {6, "VP8_RTP_RFC_7741"}, {7, "AAC_RAW"}, {8, "AAC_MPEG4_GENERIC"}, {9, "AAC_ADTS"}, {10, "AAC_LATM"}, {11, "OPUS"}, {12, "OPUS_RTP_RFC_7587"}, {13, "MP3"}, {14, "JPEG"}, {15, "PNG"}, {16, "WEBP"}, {17, "ID3v2"}, {18, "OVEN_EVENT"}, {19, "CUE"}, {20, "AMF"}, {21, "SEI"}}},
		{"v0.18.1.1",
		 {{0, "None"}, {1, "H264"}, {2, "H265"}, {3, "Vp8"}, {4, "Vp9"}, {5, "Flv"}, {6, "Aac"}, {7, "Mp3"}, {8, "Opus"}, {9, "Jpeg"}, {10, "Png"}, {11, "Webp"}},
		 {{-1, "Unknown"}, {0, "H264_AVCC"}, {1, "H264_ANNEXB"}, {2, "H264_RTP_RFC_6184"}, {3, "HVCC"}, {4, "H265_ANNEXB"}, {5, "H265_RTP_RFC_7798"}, {6, "VP8"}, {7, "VP8_RTP_RFC_7741"}, {8, "AAC_RAW"}, {9, "AAC_MPEG4_GENERIC"}, {10, "AAC_ADTS"}, {11, "AAC_LATM"}, {12, "OPUS"}, {13, "OPUS_RTP_RFC_7587"}, {14, "MP3"}, {15, "JPEG"}, {16, "PNG"}, {17, "WEBP"}, {18, "ID3v2"}, {19, "OVEN_EVENT"}, {20, "CUE"}, {21, "AMF"}, {22, "SEI"}, {23, "SCTE35"}}},
		{"v0.18.1.5",
		 {{0, "None"}, {1, "H264"}, {2, "H265"}, {3, "Vp8"}, {4, "Vp9"}, {5, "Av1"}, {6, "Flv"}, {7, "Aac"}, {8, "Mp3"}, {9, "Opus"}, {10, "Jpeg"}, {11, "Png"}, {12, "Webp"}},
		 {{-1, "Unknown"}, {0, "H264_AVCC"}, {1, "H264_ANNEXB"}, {2, "H264_RTP_RFC_6184"}, {3, "HVCC"}, {4, "H265_ANNEXB"}, {5, "H265_RTP_RFC_7798"}, {6, "VP8"}, {7, "VP8_RTP_RFC_7741"}, {8, "AAC_RAW"}, {9, "AAC_MPEG4_GENERIC"}, {10, "AAC_ADTS"}, {11, "AAC_LATM"}, {12, "OPUS"}, {13, "OPUS_RTP_RFC_7587"}, {14, "MP3"}, {15, "JPEG"}, {16, "PNG"}, {17, "WEBP"}, {18, "ID3v2"}, {19, "OVEN_EVENT"}, {20, "CUE"}, {21, "AMF"}, {22, "SEI"}, {23, "SCTE35"}}},
		{"v0.20.8.0",
		 {{0, "None"}, {1, "H264"}, {2, "H265"}, {3, "Vp8"}, {4, "Vp9"}, {5, "Av1"}, {6, "Flv"}, {7, "Aac"}, {8, "Mp3"}, {9, "Opus"}, {10, "Jpeg"}, {11, "Png"}, {12, "Webp"}, {13, "WebVTT"}, {14, "Whisper"}, {15, "Mp2"}},
		 {{-1, "Unknown"}, {0, "H264_AVCC"}, {1, "H264_ANNEXB"}, {2, "H264_RTP_RFC_6184"}, {3, "HVCC"}, {4, "H265_ANNEXB"}, {5, "H265_RTP_RFC_7798"}, {6, "VP8"}, {7, "VP8_RTP_RFC_7741"}, {8, "AAC_RAW"}, {9, "AAC_MPEG4_GENERIC"}, {10, "AAC_ADTS"}, {11, "AAC_LATM"}, {12, "OPUS"}, {13, "OPUS_RTP_RFC_7587"}, {14, "MP3"}, {15, "JPEG"}, {16, "PNG"}, {17, "WEBP"}, {18, "ID3v2"}, {19, "OVEN_EVENT"}, {20, "CUE"}, {21, "AMF"}, {22, "SEI"}, {23, "SCTE35"}, {24, "WebVTT"}, {25, "MP2"}, {26, "AV1_OBU"}, {27, "AV1_RTP_AOM"}}},
		{"v0.21.0.0",
		 {{0, "None"}, {1, "H264"}, {2, "H265"}, {3, "Vp8"}, {4, "Vp9"}, {5, "Av1"}, {6, "Flv"}, {7, "Aac"}, {8, "Mp3"}, {9, "Opus"}, {10, "Jpeg"}, {11, "Png"}, {12, "Webp"}, {13, "WebVTT"}, {14, "Whisper"}, {15, "Mp2"}, {16, "Avif"}},
		 {{-1, "Unknown"}, {0, "H264_AVCC"}, {1, "H264_ANNEXB"}, {2, "H264_RTP_RFC_6184"}, {3, "HVCC"}, {4, "H265_ANNEXB"}, {5, "H265_RTP_RFC_7798"}, {6, "VP8"}, {7, "VP8_RTP_RFC_7741"}, {8, "AAC_RAW"}, {9, "AAC_MPEG4_GENERIC"}, {10, "AAC_ADTS"}, {11, "AAC_LATM"}, {12, "OPUS"}, {13, "OPUS_RTP_RFC_7587"}, {14, "MP3"}, {15, "JPEG"}, {16, "PNG"}, {17, "WEBP"}, {18, "ID3v2"}, {19, "OVEN_EVENT"}, {20, "CUE"}, {21, "AMF"}, {22, "SEI"}, {23, "SCTE35"}, {24, "WebVTT"}, {25, "MP2"}, {26, "AV1_OBU"}, {27, "AV1_RTP_AOM"}, {28, "AVIF"}}},
	};

	const char *ReadCodecId(const ReleaseTable &release, uint8_t wire)
	{
		// `MediaCodecId` is `uint8_t`-based in every release, so the JSON integer is read as-is
		auto it = release.codec_id.find(wire);
		return (it != release.codec_id.end()) ? it->second : "(out of range)";
	}

	const char *ReadBitstreamFormat(const ReleaseTable &release, uint8_t wire)
	{
		// `BitstreamFormat` is `int8_t`-based in every release, so 0xFF reads back as -1
		auto it = release.bitstream_format.find(static_cast<int8_t>(wire));
		return (it != release.bitstream_format.end()) ? it->second : "(out of range)";
	}

	// The structural describe rules of the edge parser, identical in every release from `v0.15.8.1` to `v0.21.0.0`
	// (each tag's `providers/ovt/ovt_stream.cpp`).
	// A failure here is a describe the release rejects outright.
	bool ParsesWithReleaseRules(const Json::Value &contents)
	{
		if (contents["version"].isUInt() == false)
		{
			return false;
		}

		const auto &stream = contents["stream"];
		if (stream["appName"].isNull() || stream["streamName"].isNull() || stream["tracks"].isArray() == false)
		{
			return false;
		}

		for (const auto &playlist : stream["playlists"])
		{
			if (playlist["name"].isNull() || playlist["fileName"].isNull() || playlist["options"].isObject() == false || playlist["renditions"].isArray() == false)
			{
				return false;
			}

			for (const auto &rendition : playlist["renditions"])
			{
				if (rendition["name"].isString() == false || rendition["videoTrackName"].isString() == false || rendition["audioTrackName"].isString() == false)
				{
					return false;
				}
			}
		}

		for (const auto &track : stream["tracks"])
		{
			if (track["id"].isUInt() == false || track["name"].isString() == false || track["codecId"].isUInt() == false || track["mediaType"].isUInt() == false ||
				track["timebaseNum"].isUInt() == false || track["timebaseDen"].isUInt() == false || track["bitrate"].isUInt() == false ||
				track["startFrameTime"].isUInt64() == false || track["lastFrameTime"].isUInt64() == false)
			{
				return false;
			}

			if ((track["mediaType"].asUInt() == 0) && track["videoTrack"].isNull())
			{
				return false;
			}

			if ((track["mediaType"].asUInt() == 1) && track["audioTrack"].isNull())
			{
				return false;
			}
		}

		return true;
	}
}  // namespace

// The OVT2 form adds the name strings an OVT2 edge decides on, and nothing else changes
TEST(OvtDescribeTest, Ovt2FormCarriesTheNameStrings)
{
	auto contents	  = BuildFixtureDescription();
	const auto &video = contents["stream"]["tracks"][0];
	const auto &audio = contents["stream"]["tracks"][1];

	EXPECT_STREQ(video["codec"].asCString(), "H264");
	EXPECT_STREQ(video["mediaTypeName"].asCString(), "Video");
	EXPECT_STREQ(video["audioTrack"]["sampleFormatName"].asCString(), "none");
	// H264 derives its `codecs` string from the decoder configuration record, which this fixture
	// track has none of. A key is written only when a string was derived. See `CodecsCarriesTheDerivedString`.
	EXPECT_TRUE(MakeVideoTrack()->GetCodecsParameter().IsEmpty());
	EXPECT_FALSE(video.isMember("codecs"));

	EXPECT_STREQ(audio["codec"].asCString(), "AAC");
	EXPECT_STREQ(audio["mediaTypeName"].asCString(), "Audio");
	EXPECT_STREQ(audio["audioTrack"]["sampleFormatName"].asCString(), "fltp");

	// The value, not just the type. This field exists because the origin's setting used to be lost
	// on the way to the edge, so a describe that always said `true` would look the same as one that
	// carried nothing. The fixture playlist leaves it at its default.
	EXPECT_EQ(contents["stream"]["playlists"][0]["enableSubtitles"].asBool(), true);

	std::map<ov::String, std::shared_ptr<const info::Playlist>> subtitles_off_playlists;
	{
		auto off = std::make_shared<info::Playlist>("ABR", "abr", false);
		off->EnableSubtitles(false);
		subtitles_off_playlists.emplace("abr", off);
	}
	auto subtitles_off = OvtStream::BuildDescription("app", "stream", "origin-uuid", subtitles_off_playlists,
													 {{1, MakeVideoTrack()}});
	EXPECT_EQ(subtitles_off["stream"]["playlists"][0]["enableSubtitles"].asBool(), false);
}

// `codecs` carries the RFC 6381 string this build derives, and nothing else, because that is what
// makes the edge's comparison with its own derivation mean anything. Opus derives one from the codec
// alone, so this pins a written value rather than an absent key.
TEST(OvtDescribeTest, CodecsCarriesTheDerivedString)
{
	auto track = std::make_shared<MediaTrack>();
	track->SetId(3);
	track->SetCodecId(cmn::MediaCodecId::Opus);
	track->SetMediaType(cmn::MediaType::Audio);
	track->SetVariantName("opus");
	track->SetTimeBase(1, 48000);
	track->SetSampleRate(48000);
	track->SetSampleFormat(cmn::AudioSample::Format::FltP);
	track->SetChannelLayout(cmn::AudioChannel::Layout::LayoutStereo);

	const auto derived = track->GetCodecsParameter();
	ASSERT_FALSE(derived.IsEmpty());

	Json::Value json_track;
	OvtStream::GenerateTrackDescription(track, json_track);

	ASSERT_TRUE(json_track.isMember("codecs"));
	EXPECT_STREQ(json_track["codecs"].asCString(), derived.CStr());
}

namespace
{
	// Two video variants and a two-language audio group,
	// with the `abr` playlist whose renditions pick audio by index.
	// Track ids are what `ResolveRenditionTrackIds()` would assign.
	Json::Value BuildTrackSetFixture()
	{
		auto make = [](uint32_t id, cmn::MediaType type, const char *variant, cmn::MediaCodecId codec) {
			auto track = std::make_shared<MediaTrack>();
			track->SetId(id);
			track->SetMediaType(type);
			track->SetVariantName(variant);
			track->SetCodecId(codec);
			track->SetTimeBase(1, 90000);
			return track;
		};

		std::map<int32_t, std::shared_ptr<const MediaTrack>> tracks = {
			{0, make(0, cmn::MediaType::Video, "720p", cmn::MediaCodecId::H264)},
			{1, make(1, cmn::MediaType::Video, "360p", cmn::MediaCodecId::H264)},
			{2, make(2, cmn::MediaType::Audio, "aac", cmn::MediaCodecId::Aac)},	 // ko
			{3, make(3, cmn::MediaType::Audio, "aac", cmn::MediaCodecId::Aac)},	 // en
		};

		auto playlist = std::make_shared<info::Playlist>("ABR", "abr", false);

		auto r720_ko  = std::make_shared<info::Rendition>("720p_ko", "720p", "aac");
		r720_ko->SetVideoIndexHint(-1);
		r720_ko->SetVideoTrackId(0);
		r720_ko->SetAudioIndexHint(0);
		r720_ko->SetAudioTrackId(2);
		playlist->AddRendition(r720_ko);

		auto r360_en = std::make_shared<info::Rendition>("360p_en", "360p", "aac");
		r360_en->SetVideoIndexHint(-1);
		r360_en->SetVideoTrackId(1);
		r360_en->SetAudioIndexHint(1);
		r360_en->SetAudioTrackId(3);
		playlist->AddRendition(r360_en);

		auto audio_en = std::make_shared<info::Rendition>("audio_en", "", "aac");
		audio_en->SetAudioIndexHint(1);
		audio_en->SetAudioTrackId(3);
		playlist->AddRendition(audio_en);

		std::map<ov::String, std::shared_ptr<const info::Playlist>> playlists = {{"abr", playlist}};
		return OvtStream::BuildDescription("app", "stream", "origin-uuid", playlists, tracks);
	}
}  // namespace

// `ovt.required` names the codecs of the configured media types, sorted and unique
TEST(OvtDescribeTest, RequiredTokensFollowTheConfiguredMediaTypes)
{
	auto contents = BuildTrackSetFixture();

	auto tokens	  = OvtStream::CollectRequiredTokens(contents, {cmn::MediaType::Video, cmn::MediaType::Audio});
	ASSERT_EQ(tokens.size(), 2u);
	EXPECT_EQ(tokens[0], ov::String("codec/AAC"));
	EXPECT_EQ(tokens[1], ov::String("codec/H264"));

	auto video_only = OvtStream::CollectRequiredTokens(contents, {cmn::MediaType::Video});
	ASSERT_EQ(video_only.size(), 1u);
	EXPECT_EQ(video_only[0], ov::String("codec/H264"));

	EXPECT_TRUE(OvtStream::CollectRequiredTokens(contents, {}).empty());

	// After a filter the list names only what is left
	OvtStream::RebuildRenditions(contents["stream"], {3});
	auto filtered = OvtStream::CollectRequiredTokens(contents, {cmn::MediaType::Video, cmn::MediaType::Audio});
	ASSERT_EQ(filtered.size(), 1u);
	EXPECT_EQ(filtered[0], ov::String("codec/AAC"));
}

// PT 40 delivery rule: a session sends the current set once before its first media,
// nothing more while the set is unchanged, and once again after a change
TEST(OvtRequiredTest, SetVersionAndCursor)
{
	OvtRequiredSet set;
	OvtRequiredCursor new_session;

	auto first = set.Get();
	EXPECT_TRUE(first.tokens->empty());
	EXPECT_TRUE(new_session.NeedsSend(first.version));
	new_session.MarkSent(first.version);
	EXPECT_FALSE(new_session.NeedsSend(set.Get().version));

	// Re-adding a known token changes nothing
	EXPECT_TRUE(set.Add("bitstream/AAC_XYZ"));
	EXPECT_FALSE(set.Add("bitstream/AAC_XYZ"));
	auto changed = set.Get();
	EXPECT_NE(changed.version, first.version);
	ASSERT_EQ(changed.tokens->size(), 1u);
	EXPECT_TRUE(new_session.NeedsSend(changed.version));
	new_session.MarkSent(changed.version);
	EXPECT_FALSE(new_session.NeedsSend(set.Get().version));

	// A session that joins later has never sent anything and gets the current set
	OvtRequiredCursor late_session;
	EXPECT_TRUE(late_session.NeedsSend(set.Get().version));

	// Snapshots are immutable: the one taken before the change still holds the old list
	EXPECT_TRUE(first.tokens->empty());
}

// TrackSet { 360p, en } = tracks { 1, 3 }: the rendition pointing at removed tracks goes,
// the two that survive keep their track ids,
// and the hints stay untouched for the OVT2 edge
TEST(OvtDescribeTest, RebuildRenditionsForTrackSet)
{
	auto contents		= BuildTrackSetFixture();
	Json::Value &stream = contents["stream"];

	auto pruned			= OvtStream::RebuildRenditions(stream, {1, 3});

	EXPECT_EQ(pruned, 1u);
	ASSERT_EQ(stream["tracks"].size(), 2u);
	EXPECT_EQ(stream["tracks"][0]["id"].asUInt(), 1u);
	EXPECT_EQ(stream["tracks"][1]["id"].asUInt(), 3u);

	ASSERT_EQ(stream["playlists"].size(), 1u);
	const auto &renditions = stream["playlists"][0]["renditions"];
	ASSERT_EQ(renditions.size(), 2u);

	EXPECT_STREQ(renditions[0]["name"].asCString(), "360p_en");
	EXPECT_STREQ(renditions[0]["videoTrackName"].asCString(), "360p");
	EXPECT_EQ(renditions[0]["videoIndexHint"].asInt(), -1);
	EXPECT_EQ(renditions[0]["videoTrackId"].asUInt(), 1u);
	EXPECT_EQ(renditions[0]["audioIndexHint"].asInt(), 1);
	EXPECT_EQ(renditions[0]["audioTrackId"].asUInt(), 3u);

	EXPECT_STREQ(renditions[1]["name"].asCString(), "audio_en");
	EXPECT_STREQ(renditions[1]["videoTrackName"].asCString(), "");
	EXPECT_EQ(renditions[1]["audioTrackId"].asUInt(), 3u);
}

// Two renditions that resolve to the same tracks after filtering collapse into one
TEST(OvtDescribeTest, RebuildRenditionsPrunesDuplicates)
{
	auto contents		= BuildTrackSetFixture();
	Json::Value &stream = contents["stream"];

	// Audio only: 360p_en loses its video and becomes { 3 }, the same as audio_en
	auto pruned			= OvtStream::RebuildRenditions(stream, {3});

	EXPECT_EQ(pruned, 2u);
	const auto &renditions = stream["playlists"][0]["renditions"];
	ASSERT_EQ(renditions.size(), 1u);
	EXPECT_STREQ(renditions[0]["name"].asCString(), "360p_en");
	EXPECT_STREQ(renditions[0]["videoTrackName"].asCString(), "");
	EXPECT_EQ(renditions[0]["audioTrackId"].asUInt(), 3u);
}

// Only the 720p video survives: two renditions go and the third loses its audio side
TEST(OvtDescribeTest, RebuildRenditionsKeepsAVideoOnlyRendition)
{
	auto contents		= BuildTrackSetFixture();
	Json::Value &stream = contents["stream"];

	auto pruned			= OvtStream::RebuildRenditions(stream, {0});

	// 360p_en and audio_en go (their tracks are gone); 720p_ko survives as video only
	EXPECT_EQ(pruned, 2u);
	ASSERT_EQ(stream["playlists"].size(), 1u);
	ASSERT_EQ(stream["playlists"][0]["renditions"].size(), 1u);
	EXPECT_STREQ(stream["playlists"][0]["renditions"][0]["audioTrackName"].asCString(), "");
}

// Nothing survives: every rendition and then the playlist itself is removed
TEST(OvtDescribeTest, RebuildRenditionsDropsEmptyPlaylist)
{
	auto contents		= BuildTrackSetFixture();
	Json::Value &stream = contents["stream"];

	// Only the ko audio: 720p_ko loses its video and keeps ko, so the playlist survives with one rendition
	auto pruned			= OvtStream::RebuildRenditions(stream, {2});
	EXPECT_EQ(pruned, 2u);
	ASSERT_EQ(stream["playlists"].size(), 1u);
	ASSERT_EQ(stream["playlists"][0]["renditions"].size(), 1u);
	EXPECT_STREQ(stream["playlists"][0]["renditions"][0]["name"].asCString(), "720p_ko");

	// No track at all: the three renditions and the playlist are pruned (3 + 1)
	auto empty = BuildTrackSetFixture();
	EXPECT_EQ(OvtStream::RebuildRenditions(empty["stream"], {}), 4u);
	EXPECT_TRUE(empty["stream"]["tracks"].empty());
	EXPECT_TRUE(empty["stream"]["playlists"].empty());
	EXPECT_TRUE(empty["stream"]["playlists"].isArray());
}

// For an OVT1 edge the hints are rewritten to the group indices that edge will assign.
// The OVT2 keys stay on the wire: an OVT1 edge ignores what it does not know.
// What an OVT1 edge may carry, judged on the describe entry that is about to be sent.
// The same judgment picks the tracks and writes the body, so the two cannot disagree.
TEST(OvtDescribeTest, LegacyCarriabilityIsJudgedOnTheDescribeEntry)
{
	auto entry = [](const char *media_type_name, const char *codec) {
		Json::Value track;
		track["mediaTypeName"] = media_type_name;
		track["codec"]		   = codec;
		return track;
	};

	// Every codec shipped up to v0.21.0.0 has an OVT1 wire value, `AVIF` being the last one added.
	// The name is the `GetCodecIdString()` spelling, not the C++ enumerator.
	EXPECT_TRUE(OvtStream::IsCarriableByLegacyEdge(entry("Video", "H264")));
	EXPECT_TRUE(OvtStream::IsCarriableByLegacyEdge(entry("Audio", "AAC")));
	EXPECT_TRUE(OvtStream::IsCarriableByLegacyEdge(entry("Video", "AVIF")));

	// A name this build does not know cannot have one either
	EXPECT_FALSE(OvtStream::IsCarriableByLegacyEdge(entry("Video", "DOES-NOT-EXIST")));
	EXPECT_FALSE(OvtStream::IsCarriableByLegacyEdge(entry("Video", "Avif")));  // enumerator, not the name
	EXPECT_FALSE(OvtStream::IsCarriableByLegacyEdge(entry("Video", "h264")));  // exact spelling only
	EXPECT_FALSE(OvtStream::IsCarriableByLegacyEdge(entry("Video", "")));

	// `None` shipped in v0.21.0.0 like the rest, so it passes on its own.
	// The `Data` exemption is not about the codec at all: it is there because a `Data` track is
	// described with `codec: "None"`, and this build must not start judging it on that.
	EXPECT_TRUE(OvtStream::IsCarriableByLegacyEdge(entry("Data", "None")));
	EXPECT_TRUE(OvtStream::IsCarriableByLegacyEdge(entry("Video", "None")));
	// A `Data` track passes even when its codec name is one no build knows
	EXPECT_TRUE(OvtStream::IsCarriableByLegacyEdge(entry("Data", "DOES-NOT-EXIST")));

	// Subtitle is not exempt: it is judged on its codec like video and audio
	EXPECT_TRUE(OvtStream::IsCarriableByLegacyEdge(entry("Subtitle", "WebVTT")));
	EXPECT_FALSE(OvtStream::IsCarriableByLegacyEdge(entry("Subtitle", "DOES-NOT-EXIST")));
}

// A side the filter empties carries the variant it used to name, so the receiver can tell it apart
// from a side the operator left empty on purpose. The two look identical without it.
TEST(OvtDescribeTest, AnEmptiedSideNamesTheVariantItLost)
{
	auto contents = BuildTrackSetFixture();
	// Keeps the 720p video and the `en` audio, so each of the first two renditions loses one side
	OvtStream::RebuildRenditions(contents["stream"], {0, 3});

	const auto &renditions = contents["stream"]["playlists"][0]["renditions"];
	ASSERT_EQ(renditions.size(), 2u);

	EXPECT_STREQ(renditions[0]["name"].asCString(), "720p_ko");
	EXPECT_STREQ(renditions[0]["audioTrackName"].asCString(), "");
	EXPECT_STREQ(renditions[0]["removedAudioTrackName"].asCString(), "aac");
	EXPECT_FALSE(renditions[0].isMember("removedVideoTrackName"));

	EXPECT_STREQ(renditions[1]["name"].asCString(), "360p_en");
	EXPECT_STREQ(renditions[1]["videoTrackName"].asCString(), "");
	EXPECT_STREQ(renditions[1]["removedVideoTrackName"].asCString(), "360p");
	EXPECT_FALSE(renditions[1].isMember("removedAudioTrackName"));

	// `audio_en` was written audio-only, so neither side was emptied and it carries no marker.
	// It is gone all the same: `360p_en` lost its video and now resolves to the same single track,
	// so the duplicate rule drops whichever comes second. One-sided renditions are kept on purpose
	// (decision: the origin decides what is worth sending), and this is the cost of that.
	for (const auto &rendition : renditions)
	{
		EXPECT_STRNE(rendition["name"].asCString(), "audio_en");
	}
}

TEST(OvtDescribeTest, RenumbersHintsForFilteredDescribe)
{
	auto contents = BuildTrackSetFixture();
	OvtStream::RebuildRenditions(contents["stream"], {1, 3});
	OvtStream::RenumberIndexHints(contents["stream"]);

	const auto &renditions = contents["stream"]["playlists"][0]["renditions"];
	ASSERT_EQ(renditions.size(), 2u);

	// en is now the only aac track the edge adds, so it becomes index 0; 360p is the only 360p track
	EXPECT_EQ(renditions[0]["videoIndexHint"].asInt(), 0);
	EXPECT_EQ(renditions[0]["audioIndexHint"].asInt(), 0);
	EXPECT_EQ(renditions[1]["audioIndexHint"].asInt(), 0);

	EXPECT_TRUE(renditions[0]["videoTrackId"].isUInt());
	EXPECT_TRUE(renditions[0]["audioTrackId"].isUInt());
	for (const auto &track : contents["stream"]["tracks"])
	{
		EXPECT_TRUE(track.isMember("codec"));
	}
}

TEST(OvtDescribeTest, EveryReleaseParsesTheStructure)
{
	ASSERT_EQ(RELEASES.size(), 8u);

	// What an OVT1 edge receives is the OVT2 form as it is, unfiltered
	auto contents = BuildFixtureDescription();
	EXPECT_TRUE(ParsesWithReleaseRules(contents));

	// ... and the filtered form with rebuilt renditions and renumbered hints
	auto filtered = BuildTrackSetFixture();
	OvtStream::RebuildRenditions(filtered["stream"], {1, 3});
	OvtStream::RenumberIndexHints(filtered["stream"]);
	EXPECT_TRUE(ParsesWithReleaseRules(filtered));

	// An emptied rendition side is still a string, which every release requires
	auto video_only = BuildTrackSetFixture();
	OvtStream::RebuildRenditions(video_only["stream"], {0});
	OvtStream::RenumberIndexHints(video_only["stream"]);
	EXPECT_TRUE(ParsesWithReleaseRules(video_only));
	EXPECT_TRUE(video_only["stream"]["playlists"][0]["renditions"][0]["audioTrackName"].isString());
}

// How each release reads the values this publisher sends for an H264 + AAC stream.
// `v0.18.1.5` and later read them as intended.
// Earlier releases read AAC as MP3, because the `Av1` enumerator `v0.18.1.5` inserted shifted the codec ids.
// Releases up to `v0.18.0` read the ADTS transport format as another one, from two `BitstreamFormat` shifts.
// Those rows pin the state the design leaves in place rather than approve it.
TEST(OvtDescribeTest, EachReleaseReadsTheValuesAsRecorded)
{
	struct Expectation
	{
		const char *tag;
		const char *aac_codec;
		const char *aac_adts_format;
	};

	const std::vector<Expectation> expectations = {
		{"v0.15.8.1", "Mp3", "OPUS"},
		{"v0.15.12", "Mp3", "OPUS"},
		{"v0.17.3.1", "Mp3", "OPUS"},
		{"v0.18.0", "Mp3", "AAC_LATM"},
		{"v0.18.1.1", "Mp3", "AAC_ADTS"},
		{"v0.18.1.5", "Aac", "AAC_ADTS"},
		{"v0.20.8.0", "Aac", "AAC_ADTS"},
		{"v0.21.0.0", "Aac", "AAC_ADTS"},
	};

	auto contents		  = BuildFixtureDescription();
	auto video_codec_wire = static_cast<uint8_t>(contents["stream"]["tracks"][0]["codecId"].asUInt());
	auto audio_codec_wire = static_cast<uint8_t>(contents["stream"]["tracks"][1]["codecId"].asUInt());
	ASSERT_EQ(video_codec_wire, ovt::ToOvtWire(cmn::MediaCodecId::H264));
	ASSERT_EQ(audio_codec_wire, ovt::ToOvtWire(cmn::MediaCodecId::Aac));

	for (const auto &expectation : expectations)
	{
		auto release = std::find_if(RELEASES.begin(), RELEASES.end(), [&](const ReleaseTable &table) { return ov::String(table.tag) == expectation.tag; });
		ASSERT_TRUE(release != RELEASES.end()) << expectation.tag;

		EXPECT_STREQ(ReadCodecId(*release, video_codec_wire), "H264") << release->tag;
		EXPECT_STREQ(ReadCodecId(*release, audio_codec_wire), expectation.aac_codec) << release->tag;
		EXPECT_STREQ(ReadBitstreamFormat(*release, ovt::ToOvtWire(cmn::BitstreamFormat::H264_ANNEXB)), "H264_ANNEXB") << release->tag;
		EXPECT_STREQ(ReadBitstreamFormat(*release, ovt::ToOvtWire(cmn::BitstreamFormat::AAC_ADTS)), expectation.aac_adts_format) << release->tag;
	}
}

// ---- More describe fixtures ----

namespace
{
	std::map<ov::String, std::shared_ptr<const info::Playlist>> NoPlaylists()
	{
		return {};
	}

	std::shared_ptr<MediaTrack> MakeDataTrack()
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(9);
		track->SetMediaType(cmn::MediaType::Data);
		track->SetVariantName("data");
		track->SetTimeBase(1, 1000);
		return track;
	}
}  // namespace

// `playlists` is an array whether or not the stream has one. `v0.21.0.0` wrote null in the empty case.
// Every release from `v0.15.0` on reads the key with `size()` and indexed access,
// where null and an empty array are the same thing.
TEST(OvtDescribeTest, NoPlaylistWritesEmptyArray)
{
	std::map<int32_t, std::shared_ptr<const MediaTrack>> tracks = {{1, MakeVideoTrack()}};
	auto contents												= OvtStream::BuildDescription("app", "stream", "u", NoPlaylists(), tracks);

	auto text													= ov::Json::Stringify(contents);
	EXPECT_NE(text.IndexOf("\"playlists\":[]"), -1) << text.CStr();
	EXPECT_EQ(contents["stream"]["playlists"].isArray(), true);
	EXPECT_EQ(contents["stream"]["tracks"].size(), 1u);
}

// A data track has codec `None`, media type `Data`, and the audio placeholder; the OVT2 strings say so
TEST(OvtDescribeTest, DataTrackForm)
{
	std::map<int32_t, std::shared_ptr<const MediaTrack>> tracks = {{9, MakeDataTrack()}};
	auto contents												= OvtStream::BuildDescription("app", "stream", "u", NoPlaylists(), tracks);
	const auto &track											= contents["stream"]["tracks"][0];

	EXPECT_EQ(track["codecId"].asUInt(), 0u);
	EXPECT_EQ(track["mediaType"].asInt(), 2);
	EXPECT_STREQ(track["codec"].asCString(), "None");
	EXPECT_STREQ(track["mediaTypeName"].asCString(), "Data");
	EXPECT_EQ(track["audioTrack"]["sampleFormat"].asInt(), -1);
	EXPECT_STREQ(track["audioTrack"]["sampleFormatName"].asCString(), "none");
	EXPECT_EQ(track["videoTrack"]["width"].asUInt(), 0u);

	// It is never a required codec
	EXPECT_TRUE(OvtStream::CollectRequiredTokens(contents, {cmn::MediaType::Video, cmn::MediaType::Audio}).empty());
	// Only when the operator asks for data tracks explicitly
	auto with_data = OvtStream::CollectRequiredTokens(contents, {cmn::MediaType::Data});
	ASSERT_EQ(with_data.size(), 1u);
	EXPECT_EQ(with_data[0], ov::String("codec/None"));
}

// An unfiltered describe is sent as generated: no track left its group, so every hint still points
// at the track it was built for.
TEST(OvtDescribeTest, UnfilteredDescribeKeepsHints)
{
	auto contents		   = BuildTrackSetFixture();

	const auto &renditions = contents["stream"]["playlists"][0]["renditions"];
	EXPECT_EQ(renditions[0]["videoIndexHint"].asInt(), -1);
	EXPECT_EQ(renditions[0]["audioIndexHint"].asInt(), 0);
	EXPECT_EQ(renditions[1]["audioIndexHint"].asInt(), 1);
	EXPECT_TRUE(renditions[1]["audioTrackId"].isUInt());
}

// Playlists are written in fileName order, renditions in configuration order
TEST(OvtDescribeTest, PlaylistOrderFollowsFileName)
{
	auto b = std::make_shared<info::Playlist>("B", "b_list", false);
	b->AddRendition(std::make_shared<info::Rendition>("r1", "pt_video", ""));
	auto a = std::make_shared<info::Playlist>("A", "a_list", false);
	a->AddRendition(std::make_shared<info::Rendition>("second", "pt_video", ""));
	a->AddRendition(std::make_shared<info::Rendition>("first", "", "pt_audio"));

	std::map<ov::String, std::shared_ptr<const info::Playlist>> playlists = {{"b_list", b}, {"a_list", a}};
	std::map<int32_t, std::shared_ptr<const MediaTrack>> tracks			  = {{1, MakeVideoTrack()}, {2, MakeAudioTrack()}};
	auto contents														  = OvtStream::BuildDescription("app", "stream", "u", playlists, tracks);

	const auto &json_playlists											  = contents["stream"]["playlists"];
	ASSERT_EQ(json_playlists.size(), 2u);
	EXPECT_STREQ(json_playlists[0]["fileName"].asCString(), "a_list");
	EXPECT_STREQ(json_playlists[0]["renditions"][0]["name"].asCString(), "second");
	EXPECT_STREQ(json_playlists[0]["renditions"][1]["name"].asCString(), "first");
	EXPECT_STREQ(json_playlists[1]["fileName"].asCString(), "b_list");
}

// A rendition without any track id is left alone by the rebuild while its variants survive
TEST(OvtDescribeTest, RebuildKeepsIdLessRenditionsWhoseVariantsSurvive)
{
	auto playlist  = std::make_shared<info::Playlist>("P", "p", false);
	auto rendition = std::make_shared<info::Rendition>("r", "pt_video", "pt_audio");
	playlist->AddRendition(rendition);
	std::map<ov::String, std::shared_ptr<const info::Playlist>> playlists = {{"p", playlist}};
	std::map<int32_t, std::shared_ptr<const MediaTrack>> tracks			  = {{1, MakeVideoTrack()}, {2, MakeAudioTrack()}};
	auto contents														  = OvtStream::BuildDescription("app", "stream", "u", playlists, tracks);

	EXPECT_EQ(OvtStream::RebuildRenditions(contents["stream"], {1, 2}), 0u);
	const auto &kept = contents["stream"]["playlists"][0]["renditions"][0];
	EXPECT_STREQ(kept["videoTrackName"].asCString(), "pt_video");
	EXPECT_STREQ(kept["audioTrackName"].asCString(), "pt_audio");
	EXPECT_EQ(kept["videoIndexHint"].asInt(), -1);

	// Dropping the audio track empties only that side
	EXPECT_EQ(OvtStream::RebuildRenditions(contents["stream"], {1}), 0u);
	const auto &video_only = contents["stream"]["playlists"][0]["renditions"][0];
	EXPECT_STREQ(video_only["videoTrackName"].asCString(), "pt_video");
	EXPECT_STREQ(video_only["audioTrackName"].asCString(), "");
	EXPECT_EQ(video_only["audioIndexHint"].asInt(), -1);
}

// Rebuild with every track allowed is a no-op on the JSON
TEST(OvtDescribeTest, RebuildWithEverythingAllowedIsNoOp)
{
	auto contents = BuildTrackSetFixture();
	auto before	  = ov::Json::Stringify(contents);
	EXPECT_EQ(OvtStream::RebuildRenditions(contents["stream"], {0, 1, 2, 3}), 0u);
	EXPECT_EQ(ov::Json::Stringify(contents), before);
}

// Renumbering follows the track order per variant and leaves sides without an id untouched
TEST(OvtDescribeTest, RenumberFollowsTrackOrderPerVariant)
{
	Json::Value stream;
	for (auto [id, name] : std::vector<std::pair<uint32_t, const char *>>{{5, "a"}, {7, "b"}, {9, "a"}, {11, "a"}})
	{
		Json::Value track;
		track["id"]	  = id;
		track["name"] = name;
		stream["tracks"].append(track);
	}
	Json::Value playlist;
	Json::Value r1;
	r1["videoTrackName"] = "a";
	r1["videoIndexHint"] = 42;
	r1["videoTrackId"]	 = 11;
	r1["audioTrackName"] = "b";
	r1["audioIndexHint"] = -1;
	playlist["renditions"].append(r1);
	Json::Value r2;
	r2["audioTrackName"] = "a";
	r2["audioIndexHint"] = 0;
	r2["audioTrackId"]	 = 9;
	playlist["renditions"].append(r2);
	Json::Value r3;
	r3["audioTrackName"] = "a";
	r3["audioIndexHint"] = 3;
	r3["audioTrackId"]	 = 999;	 // unknown id: hint left alone
	playlist["renditions"].append(r3);
	stream["playlists"].append(playlist);

	OvtStream::RenumberIndexHints(stream);

	const auto &renditions = stream["playlists"][0]["renditions"];
	EXPECT_EQ(renditions[0]["videoIndexHint"].asInt(), 2);	 // a: 5 -> 0, 9 -> 1, 11 -> 2
	EXPECT_EQ(renditions[0]["audioIndexHint"].asInt(), -1);	 // no id: untouched
	EXPECT_EQ(renditions[1]["audioIndexHint"].asInt(), 1);
	EXPECT_EQ(renditions[2]["audioIndexHint"].asInt(), 3);
}

// The play response's playlists are the describe rebuild run on the confirmed set,
// so an edge that drops a track it cannot carry gets renditions that no longer name it
TEST(OvtDescribeTest, PlayRebuildFollowsTheConfirmedSet)
{
	auto described = BuildTrackSetFixture();
	auto confirmed = described;

	// TrackSet left { 1, 3 }; the edge then declined track 1
	OvtStream::RebuildRenditions(described["stream"], {1, 3});
	OvtStream::RebuildRenditions(confirmed["stream"], {3});

	ASSERT_EQ(described["stream"]["playlists"][0]["renditions"].size(), 2u);
	EXPECT_EQ(described["stream"]["playlists"][0]["renditions"][0]["videoTrackId"].asUInt(), 1u);

	const auto &renditions = confirmed["stream"]["playlists"][0]["renditions"];
	ASSERT_EQ(renditions.size(), 1u);
	EXPECT_STREQ(renditions[0]["videoTrackName"].asCString(), "");
	EXPECT_FALSE(renditions[0].isMember("videoTrackId"));
	EXPECT_EQ(renditions[0]["audioTrackId"].asUInt(), 3u);
}

// A rendition side without a track id keeps its group meaning, and the rebuild must not turn that
// into an explicit null: a reader would see a key the describe never wrote
TEST(OvtDescribeTest, RebuildDoesNotInventNullTrackIds)
{
	auto contents		= BuildTrackSetFixture();
	Json::Value &stream = contents["stream"];

	// An origin that does not fill the ids (an `indexHint` of -1 on the audio side, a Source-form
	// stream) leaves the keys out, and the rebuild reads them to decide what to keep
	for (auto &rendition : stream["playlists"][0]["renditions"])
	{
		rendition.removeMember("videoTrackId");
		rendition.removeMember("audioTrackId");
	}

	OvtStream::RebuildRenditions(stream, {0, 1, 2, 3});

	for (const auto &playlist : stream["playlists"])
	{
		for (const auto &rendition : playlist["renditions"])
		{
			EXPECT_FALSE(rendition["videoTrackId"].isNull() && rendition.isMember("videoTrackId"))
				<< rendition["name"].asCString();
			EXPECT_FALSE(rendition["audioTrackId"].isNull() && rendition.isMember("audioTrackId"))
				<< rendition["name"].asCString();
		}
	}
}

// The filter struct: inactive with nothing set, active with any one input
TEST(OvtTrackFilterTest, Activity)
{
	OvtTrackFilter none;
	EXPECT_FALSE(none.IsActive());

	OvtTrackFilter legacy;
	legacy.legacy_edge = true;
	EXPECT_TRUE(legacy.IsActive());

	OvtTrackFilter track_set;
	track_set.track_set_ids = std::set<uint32_t>{};
	EXPECT_TRUE(track_set.IsActive());

	OvtTrackFilter requested;
	requested.requested_track_ids = std::set<uint32_t>{};
	EXPECT_TRUE(requested.IsActive());
}

// The play selection is one more intersection term, never a way to widen the set
TEST(OvtTrackFilterTest, RequestedIdsIntersect)
{
	OvtTrackFilter filter;
	filter.track_set_ids	   = std::set<uint32_t>{1, 3};
	filter.requested_track_ids = std::set<uint32_t>{3, 4};

	EXPECT_FALSE(filter.Accepts(1, cmn::MediaType::Video, cmn::MediaCodecId::H264));
	EXPECT_TRUE(filter.Accepts(3, cmn::MediaType::Audio, cmn::MediaCodecId::Aac));
	// Named by the edge but outside the TrackSet: the edge cannot reach past the policy
	EXPECT_FALSE(filter.Accepts(4, cmn::MediaType::Audio, cmn::MediaCodecId::Aac));
}

// An empty selection is a selection: the edge asked for nothing, not for everything
TEST(OvtTrackFilterTest, EmptyRequestedSetAcceptsNothing)
{
	OvtTrackFilter filter;
	filter.requested_track_ids = std::set<uint32_t>{};

	EXPECT_TRUE(filter.IsActive());
	EXPECT_FALSE(filter.Accepts(1, cmn::MediaType::Video, cmn::MediaCodecId::H264));
}

// The OVT1 codec removal still applies to a track the edge asked for.
// Every codec this build knows has a v0.21.0.0 wire value, so the removal is driven here with a value
// outside the enum: that is the shape the first codec added after `v0.21.0.0` will have.
TEST(OvtTrackFilterTest, LegacyRemovalOutranksTheSelection)
{
	const auto post_r21_codec = static_cast<cmn::MediaCodecId>(200);
	ASSERT_FALSE(ovt::HasLegacyWireValue(post_r21_codec));

	OvtTrackFilter filter;
	filter.legacy_edge		   = true;
	filter.requested_track_ids = std::set<uint32_t>{1, 2, 3};

	EXPECT_TRUE(filter.Accepts(1, cmn::MediaType::Video, cmn::MediaCodecId::H264));
	// A data track carries no codec, so the codec judgment does not apply to it
	EXPECT_TRUE(filter.Accepts(2, cmn::MediaType::Data, cmn::MediaCodecId::None));
	// Asked for by the edge and inside no other term, and still removed
	EXPECT_FALSE(filter.Accepts(3, cmn::MediaType::Video, post_r21_codec));

	// An OVT2 edge gets it: the removal is the legacy term and nothing else
	OvtTrackFilter ovt2;
	ovt2.requested_track_ids = std::set<uint32_t>{3};
	EXPECT_TRUE(ovt2.Accepts(3, cmn::MediaType::Video, post_r21_codec));
}

// `trackIds` of a play request and `allowedTrackIds` of its response read by the same rule:
// no key means no selection, and one bad element never turns a selection into "everything"
TEST(OvtTrackFilterTest, TrackIdArrayShapes)
{
	Json::Value ovt;
	EXPECT_FALSE(ovt::ParseTrackIdArray(ovt["trackIds"]).has_value());

	ovt["trackIds"] = "0,2";
	EXPECT_FALSE(ovt::ParseTrackIdArray(ovt["trackIds"]).has_value());

	ovt["trackIds"] = Json::Value(Json::arrayValue);
	auto empty		= ovt::ParseTrackIdArray(ovt["trackIds"]);
	ASSERT_TRUE(empty.has_value());
	EXPECT_TRUE(empty->empty());

	ovt["trackIds"].append(0);
	ovt["trackIds"].append("two");
	ovt["trackIds"].append(2);
	ovt["trackIds"].append(0);

	size_t ignored = 0;
	auto ids	   = ovt::ParseTrackIdArray(ovt["trackIds"], &ignored);
	ASSERT_TRUE(ids.has_value());
	EXPECT_EQ(ignored, 1u);
	EXPECT_EQ(*ids, (std::set<uint32_t>{0, 2}));
}

// The send gate discards everything up to the first marker packet, so a NOTIFY broadcast while a
// session was still behind it never arrived. The cursor is the rule that decides whether that
// session is owed every track's configuration, and it must settle after one comparison either way.
TEST(OvtTrackEpochTest, CursorSettlesOnTheFirstComparison)
{
	OvtTrackEpochCursor cursor;

	// Before a describe there is nothing to compare
	EXPECT_TRUE(cursor.IsSettled());
	EXPECT_FALSE(cursor.NeedsSnapshot(1));

	// Nothing changed while the session was behind the gate: the common case owes nothing
	cursor.SetDescribeEpoch(7);
	EXPECT_FALSE(cursor.IsSettled());
	EXPECT_FALSE(cursor.NeedsSnapshot(7));
	cursor.MarkSettled();
	EXPECT_TRUE(cursor.IsSettled());

	// A later change must not re-arm a settled cursor; the broadcast reaches the session directly
	EXPECT_FALSE(cursor.NeedsSnapshot(8));
}

TEST(OvtTrackEpochTest, CursorOwesASnapshotWhenTheEpochMoved)
{
	OvtTrackEpochCursor cursor;
	cursor.SetDescribeEpoch(3);

	EXPECT_TRUE(cursor.NeedsSnapshot(4));

	// A failed send leaves it unsettled so the next packet tries again
	EXPECT_FALSE(cursor.IsSettled());
	EXPECT_TRUE(cursor.NeedsSnapshot(4));

	cursor.MarkSettled();
	EXPECT_TRUE(cursor.IsSettled());
	EXPECT_FALSE(cursor.NeedsSnapshot(4));
}

// A session whose connection described a different stream, or none, has no epoch to compare against.
// A coincidental match must not settle it, so it is owed the snapshot whatever the stream's epoch is.
TEST(OvtTrackEpochTest, CursorWithNoDescribeAlwaysOwesASnapshot)
{
	OvtTrackEpochCursor cursor;
	cursor.SetDescribeEpoch(5);
	cursor.SetDescribeUnknown();

	EXPECT_FALSE(cursor.IsSettled());
	// Including the epoch the describe had recorded before it was dropped
	EXPECT_TRUE(cursor.NeedsSnapshot(5));
	EXPECT_TRUE(cursor.NeedsSnapshot(6));

	cursor.MarkSettled();
	EXPECT_TRUE(cursor.IsSettled());
	EXPECT_FALSE(cursor.NeedsSnapshot(6));
}

// The snapshot is the same message a track change broadcasts, with every track instead of one,
// so an edge applies it through the path it already uses for NOTIFY.
TEST(OvtTrackEpochTest, SnapshotIsANotifyCarryingEveryTrack)
{
	std::map<int32_t, std::shared_ptr<const MediaTrack>> tracks;
	tracks[1]	  = MakeVideoTrack();
	tracks[2]	  = MakeAudioTrack();

	auto snapshot = OvtStream::BuildTrackSnapshot(tracks);
	ASSERT_FALSE(snapshot.IsEmpty());

	auto object = ov::Json::Parse(snapshot);
	ASSERT_FALSE(object.IsNull());
	const auto &root = object.GetJsonValue();

	EXPECT_EQ(root["application"].asString(), "notify");
	EXPECT_EQ(root["code"].asInt(), 200);
	EXPECT_EQ(root["message"].asString(), "track_changed");

	const auto &json_tracks = root["contents"]["stream"]["tracks"];
	ASSERT_TRUE(json_tracks.isArray());
	ASSERT_EQ(json_tracks.size(), 2u);
	EXPECT_EQ(json_tracks[0]["id"].asUInt(), 1u);
	EXPECT_EQ(json_tracks[1]["id"].asUInt(), 2u);

	// The OVT2 strings are present: an edge that reads names does not fall back to the integers
	EXPECT_EQ(json_tracks[0]["codec"].asString(), "H264");
	EXPECT_EQ(json_tracks[1]["mediaTypeName"].asString(), "Audio");
}

TEST(OvtTrackEpochTest, SnapshotOfNoTrackIsEmpty)
{
	EXPECT_TRUE(OvtStream::BuildTrackSnapshot({}).IsEmpty());
}
