//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/info/media_track.h>

#include <algorithm>

#include "ovt_stream.h"

// The register-or-skip rule of the OVT edge for an OVT2 describe track.
// Rows an OVT1 origin can produce never reach this function.

namespace
{
	Json::Value TrackJson(const char *codec, const char *media_type_name, const char *sample_format_name = nullptr)
	{
		Json::Value track;
		if (codec != nullptr)
		{
			track["codec"] = codec;
		}
		if (media_type_name != nullptr)
		{
			track["mediaTypeName"] = media_type_name;
		}
		if (sample_format_name != nullptr)
		{
			track["audioTrack"]["sampleFormatName"] = sample_format_name;
		}
		return track;
	}

	std::shared_ptr<MediaTrack> Track(cmn::MediaCodecId codec_id, cmn::MediaType media_type)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetCodecId(codec_id);
		track->SetMediaType(media_type);
		return track;
	}
}  // namespace

TEST(OvtTrackSkipTest, KnownCodecWithTransportFormatIsRegistered)
{
	EXPECT_FALSE(pvd::OvtStream::GetTrackSkipReason(TrackJson("H264", "Video"), Track(cmn::MediaCodecId::H264, cmn::MediaType::Video)).has_value());
	EXPECT_FALSE(pvd::OvtStream::GetTrackSkipReason(TrackJson("AAC", "Audio", "fltp"), Track(cmn::MediaCodecId::Aac, cmn::MediaType::Audio)).has_value());
	EXPECT_FALSE(pvd::OvtStream::GetTrackSkipReason(TrackJson("WebVTT", "Subtitle"), Track(cmn::MediaCodecId::WebVTT, cmn::MediaType::Subtitle)).has_value());
}

// A data track has no codec and is registered no matter what else it carries
TEST(OvtTrackSkipTest, DataTrackIsExempt)
{
	EXPECT_FALSE(pvd::OvtStream::GetTrackSkipReason(TrackJson("None", "Data"), Track(cmn::MediaCodecId::None, cmn::MediaType::Data)).has_value());
	EXPECT_FALSE(pvd::OvtStream::GetTrackSkipReason(TrackJson(nullptr, "Data"), Track(cmn::MediaCodecId::None, cmn::MediaType::Data)).has_value());
}

TEST(OvtTrackSkipTest, MissingNamesAreSkipped)
{
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson(nullptr, "Video"), Track(cmn::MediaCodecId::H264, cmn::MediaType::Video)), ov::String("missing-name"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("H264", nullptr), Track(cmn::MediaCodecId::H264, cmn::MediaType::Video)), ov::String("missing-name"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("AAC", "Audio"), Track(cmn::MediaCodecId::Aac, cmn::MediaType::Audio)), ov::String("missing-name"));
}

// A codec name this build does not know, or a codec with no transport format, is left unregistered
TEST(OvtTrackSkipTest, UnknownOrUncarriableCodecIsSkipped)
{
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("XYZ", "Video"), Track(cmn::MediaCodecId::None, cmn::MediaType::Video)), ov::String("unknown-codec"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("h264", "Video"), Track(cmn::MediaCodecId::None, cmn::MediaType::Video)), ov::String("unknown-codec"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("VP9", "Video"), Track(cmn::MediaCodecId::Vp9, cmn::MediaType::Video)), ov::String("no-transport-format"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("FLV", "Video"), Track(cmn::MediaCodecId::Flv, cmn::MediaType::Video)), ov::String("no-transport-format"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("WHISPER", "Subtitle"), Track(cmn::MediaCodecId::Whisper, cmn::MediaType::Subtitle)), ov::String("no-transport-format"));
}

// A subtitle or image track with an unknown codec is skipped like any other (only `Data` is exempt)
TEST(OvtTrackSkipTest, OnlyDataIsExemptFromTheCodecRules)
{
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("XYZ", "Subtitle"), Track(cmn::MediaCodecId::None, cmn::MediaType::Subtitle)), ov::String("unknown-codec"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("JPEG", "Video"), Track(cmn::MediaCodecId::Jpeg, cmn::MediaType::Video)), std::nullopt);
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("AVIF", "Video"), Track(cmn::MediaCodecId::Avif, cmn::MediaType::Video)), std::nullopt);
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("MP2", "Audio", "s16"), Track(cmn::MediaCodecId::Mp2, cmn::MediaType::Audio)), std::nullopt);
}

// The exemption is decided by the `mediaTypeName` string the origin sent, not by the parsed type
TEST(OvtTrackSkipTest, DataExemptionFollowsTheName)
{
	// Named `Data`: exempt even though the parsed type disagrees
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("XYZ", "Data"), Track(cmn::MediaCodecId::None, cmn::MediaType::Video)), std::nullopt);
	// Parsed as `Data` but not named so: judged like any other track
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("XYZ", "Video"), Track(cmn::MediaCodecId::None, cmn::MediaType::Data)), ov::String("unknown-codec"));
}

// Names this build does not know are skipped, not registered as `Unknown`/`None`
TEST(OvtTrackSkipTest, UnknownMediaTypeOrSampleFormatNameIsSkipped)
{
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("JPEG", "Thumbnail"), Track(cmn::MediaCodecId::Jpeg, cmn::MediaType::Unknown)), ov::String("unknown-media-type"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("AAC", "Audio", "s64p"), Track(cmn::MediaCodecId::Aac, cmn::MediaType::Audio)), ov::String("unknown-sample-format"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("AAC", "audio", "fltp"), Track(cmn::MediaCodecId::Aac, cmn::MediaType::Unknown)), ov::String("unknown-media-type"));
}

// A video track needs no sample format name; an audio track does
TEST(OvtTrackSkipTest, SampleFormatNameOnlyForAudio)
{
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("H264", "Video"), Track(cmn::MediaCodecId::H264, cmn::MediaType::Video)), std::nullopt);
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("OPUS", "Audio"), Track(cmn::MediaCodecId::Opus, cmn::MediaType::Audio)), ov::String("missing-name"));
	EXPECT_EQ(pvd::OvtStream::GetTrackSkipReason(TrackJson("OPUS", "Audio", "fltp"), Track(cmn::MediaCodecId::Opus, cmn::MediaType::Audio)), std::nullopt);
}

// The type rule of one describe or notify track.
// A value this build does not know is tolerated; a value of the wrong type is not,
// and a field a later release added may be absent.
namespace
{
	// Every mandatory field, with the types every release from v0.15.0 writes
	Json::Value CompleteTrackJson()
	{
		Json::Value track;
		track["id"]				= 1u;
		track["name"]			= "video";
		track["codecId"]		= 1u;
		track["mediaType"]		= 0u;
		track["timebaseNum"]	= 1u;
		track["timebaseDen"]	= 90000u;
		track["bitrate"]		= 2000000u;
		track["startFrameTime"] = 0u;
		track["lastFrameTime"]	= 0u;
		return track;
	}

	bool HasField(const std::vector<ov::String> &fields, const char *name)
	{
		return std::find(fields.begin(), fields.end(), ov::String(name)) != fields.end();
	}
}  // namespace

TEST(OvtInvalidTrackFieldsTest, AReleaseThatSendsOnlyTheMandatoryFieldsIsAccepted)
{
	EXPECT_TRUE(pvd::OvtStream::InvalidTrackFields(CompleteTrackJson()).empty());
}

TEST(OvtInvalidTrackFieldsTest, EveryMandatoryFieldIsNamedWhenItIsMissing)
{
	const char *mandatory[] = {"id", "name", "codecId", "mediaType", "timebaseNum",
							   "timebaseDen", "bitrate", "startFrameTime", "lastFrameTime"};

	for (const auto *field : mandatory)
	{
		auto track = CompleteTrackJson();
		track.removeMember(field);
		EXPECT_TRUE(HasField(pvd::OvtStream::InvalidTrackFields(track), field)) << field;
	}
}

// A field a later release added: absent is fine, present with the wrong type is not.
// The label a sub-object field is reported under is not the key it is looked up by.
TEST(OvtInvalidTrackFieldsTest, OptionalFieldsAreCheckedByTheirOwnKey)
{
	const struct
	{
		const char *parent;
		const char *key;
		const char *label;
	} optional[] = {
		{nullptr, "publicName", "publicName"},
		{nullptr, "language", "language"},
		{nullptr, "characteristics", "characteristics"},
		{nullptr, "decoderConfig", "decoderConfig"},
		{nullptr, "codec", "codec"},
		{nullptr, "mediaTypeName", "mediaTypeName"},
		{nullptr, "codecs", "codecs"},
		{"videoTrack", "maxWidth", "videoTrack.maxWidth"},
		{"videoTrack", "maxHeight", "videoTrack.maxHeight"},
		{"audioTrack", "sampleFormatName", "audioTrack.sampleFormatName"},
	};

	for (const auto &entry : optional)
	{
		auto absent = CompleteTrackJson();
		if (entry.parent != nullptr)
		{
			// The sub-object is present and complete; only the optional field is missing
			absent[entry.parent] = (ov::String(entry.parent) == "videoTrack")
									   ? [] { Json::Value v; v["framerate"] = 30.0; v["width"] = 1280u; v["height"] = 720u; return v; }()
									   : [] { Json::Value a; a["samplerate"] = 48000u; a["sampleFormat"] = 8; a["layout"] = 3u; return a; }();
		}
		EXPECT_FALSE(HasField(pvd::OvtStream::InvalidTrackFields(absent), entry.label)) << entry.label;

		auto wrong = absent;
		// An object is the wrong type for every one of these, whatever the right one is
		Json::Value not_a_scalar;
		not_a_scalar["x"] = 1u;
		if (entry.parent != nullptr)
		{
			wrong[entry.parent][entry.key] = not_a_scalar;
		}
		else
		{
			wrong[entry.key] = not_a_scalar;
		}
		EXPECT_TRUE(HasField(pvd::OvtStream::InvalidTrackFields(wrong), entry.label)) << entry.label;
	}
}

// Both sub-objects are checked whenever they are there, because the media type is not final
// until the OVT2 names are read
TEST(OvtInvalidTrackFieldsTest, SubObjectMandatoryFieldsAreCheckedWhenThePartIsPresent)
{
	auto track = CompleteTrackJson();
	track["videoTrack"]["framerate"] = "30";  // a string, not a number
	track["videoTrack"]["width"]	 = 1280u;
	track["videoTrack"]["height"]	 = 720u;

	EXPECT_TRUE(HasField(pvd::OvtStream::InvalidTrackFields(track), "videoTrack.framerate"));

	// Absent sub-objects are not a violation
	auto without = CompleteTrackJson();
	EXPECT_TRUE(pvd::OvtStream::InvalidTrackFields(without).empty());
}

// The origin's RFC 6381 `codecs` string against this build's own derivation.
// The value is never adopted; it only says whether the two builds read the same decoder
// configuration record the same way.
TEST(OvtCodecsCrossCheckTest, OnlyADisagreementBetweenTwoDerivedStringsCounts)
{
	Json::Value codecs = "avc1.640028";

	EXPECT_FALSE(pvd::OvtStream::CodecsDisagree("avc1.640028", codecs));
	EXPECT_TRUE(pvd::OvtStream::CodecsDisagree("avc1.42c01f", codecs));

	// This build derived nothing, so there is nothing to disagree with
	EXPECT_FALSE(pvd::OvtStream::CodecsDisagree("", codecs));

	// The origin sent nothing, or sent something that is not a string
	EXPECT_FALSE(pvd::OvtStream::CodecsDisagree("avc1.640028", Json::Value()));
	EXPECT_FALSE(pvd::OvtStream::CodecsDisagree("avc1.640028", Json::Value(7)));
	EXPECT_FALSE(pvd::OvtStream::CodecsDisagree("avc1.640028", Json::Value::nullSingleton()));

	// Compared as received: no case folding and no trimming
	EXPECT_TRUE(pvd::OvtStream::CodecsDisagree("AVC1.640028", codecs));
	EXPECT_TRUE(pvd::OvtStream::CodecsDisagree("avc1.640028 ", codecs));
}
