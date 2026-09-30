//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "ovt_signaling.h"

#include <gtest/gtest.h>

// The stop reason strings are protocol values: fixed once shipped, never numeric
TEST(OvtSignalingTest, StopReasonStringsRoundTrip)
{
	struct Row
	{
		ovt::StopReason reason;
		const char *text;
	};

	const Row rows[] = {
		{ovt::StopReason::StreamDeleted, "stream-deleted"},
		{ovt::StopReason::OriginShutdown, "origin-shutdown"},
		{ovt::StopReason::Evicted, "evicted"},
		{ovt::StopReason::Internal, "internal"},
	};

	for (const auto &row : rows)
	{
		EXPECT_STREQ(ovt::ToString(row.reason), row.text);
		EXPECT_EQ(ovt::ParseStopReason(row.text), row.reason);
	}

	EXPECT_FALSE(ovt::ParseStopReason("").has_value());
	EXPECT_FALSE(ovt::ParseStopReason("ok").has_value());
	EXPECT_FALSE(ovt::ParseStopReason("Stream-Deleted").has_value());
}

// The supported token set is exact: known names in their protocol spelling only
TEST(OvtSignalingTest, RequiredTokenSet)
{
	EXPECT_TRUE(ovt::IsSupportedRequiredToken("codec/H264"));
	EXPECT_TRUE(ovt::IsSupportedRequiredToken("codec/AAC"));
	EXPECT_TRUE(ovt::IsSupportedRequiredToken("codec/WebVTT"));
	EXPECT_TRUE(ovt::IsSupportedRequiredToken("bitstream/AAC_ADTS"));
	EXPECT_TRUE(ovt::IsSupportedRequiredToken("bitstream/AVCC"));
	EXPECT_TRUE(ovt::IsSupportedRequiredToken("packettype/NALU"));
	EXPECT_TRUE(ovt::IsSupportedRequiredToken("feature/track-notify"));
	EXPECT_TRUE(ovt::IsSupportedRequiredToken("feature/stop-reason"));

	EXPECT_FALSE(ovt::IsSupportedRequiredToken("codec/DOES-NOT-EXIST"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken("codec/h264"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken("H264"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken("packettype/BOGUS"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken("bitstream/H264_AVCC"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken("ext-field/1"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken(""));

	EXPECT_EQ(ovt::CodecToken(cmn::MediaCodecId::Aac), ov::String("codec/AAC"));
	EXPECT_EQ(ovt::BitstreamToken(cmn::BitstreamFormat::AAC_ADTS), ov::String("bitstream/AAC_ADTS"));
	EXPECT_EQ(ovt::PacketTypeToken(cmn::PacketType::EVENT), ov::String("packettype/EVENT"));
}

// PT 40 payload round trip, and rejection of anything that is not an array of strings
TEST(OvtSignalingTest, RequiredPayloadRoundTrip)
{
	auto payload = ovt::MakeRequiredPayload({"bitstream/AAC_XYZ", "codec/H264"});
	EXPECT_STREQ(payload.CStr(), "{\"required\":[\"bitstream/AAC_XYZ\",\"codec/H264\"]}");

	auto root	= ov::Json::Parse(payload).GetJsonValue();
	auto tokens = ovt::ParseRequiredTokens(root["required"]);
	ASSERT_TRUE(tokens.has_value());
	ASSERT_EQ(tokens->size(), 2u);
	EXPECT_EQ((*tokens)[0], ov::String("bitstream/AAC_XYZ"));

	EXPECT_TRUE(ovt::ParseRequiredTokens(ov::Json::Parse("{\"required\":[]}").GetJsonValue()["required"]).has_value());
	EXPECT_FALSE(ovt::ParseRequiredTokens(ov::Json::Parse("{\"required\":\"codec/H264\"}").GetJsonValue()["required"]).has_value());
	EXPECT_FALSE(ovt::ParseRequiredTokens(ov::Json::Parse("{\"required\":[1]}").GetJsonValue()["required"]).has_value());
	EXPECT_FALSE(ovt::ParseRequiredTokens(Json::Value()).has_value());
}

// Peer detection rests on the `ovt` object alone. On a response that carries one, a
// `mediaHeaderVersion` that is missing or is not a `uint32` is a broken OVT2 origin,
// which `ReceiveDescribe()` refuses.
TEST(OvtSignalingTest, OvtObjectDetection)
{
	Json::Value ovt1;
	ovt1["application"] = "describe";
	EXPECT_FALSE(ovt::HasOvtObject(ovt1));
	EXPECT_FALSE(ovt::GetMediaHeaderVersion(ovt1).has_value());

	Json::Value not_an_object;
	not_an_object["ovt"] = "2";
	EXPECT_FALSE(ovt::HasOvtObject(not_an_object));

	Json::Value array(Json::arrayValue);
	EXPECT_FALSE(ovt::HasOvtObject(array));

	Json::Value no_version;
	no_version["ovt"]["agent"] = "x";
	EXPECT_TRUE(ovt::HasOvtObject(no_version));
	EXPECT_FALSE(ovt::GetMediaHeaderVersion(no_version).has_value());

	Json::Value string_version;
	string_version["ovt"]["mediaHeaderVersion"] = "2";
	EXPECT_TRUE(ovt::HasOvtObject(string_version));
	EXPECT_FALSE(ovt::GetMediaHeaderVersion(string_version).has_value());

	// `isUInt()` is false above the `uint32` range, so a layout number too large to read is refused
	// rather than falling through to the OVT1 rules
	Json::Value huge_version;
	huge_version["ovt"]["mediaHeaderVersion"] = Json::Int64(4294967296LL);
	EXPECT_TRUE(ovt::HasOvtObject(huge_version));
	EXPECT_FALSE(ovt::GetMediaHeaderVersion(huge_version).has_value());

	Json::Value ovt2;
	ovt2["ovt"]["mediaHeaderVersion"] = 2;
	EXPECT_EQ(ovt::GetMediaHeaderVersion(ovt2), 2u);
}

// The peer generation is read from either direction and never refuses anyone,
// so anything unreadable is the oldest generation rather than an error.
TEST(OvtSignalingTest, PeerVersionNeverFails)
{
	Json::Value ovt1;
	ovt1["application"] = "describe";
	EXPECT_EQ(ovt::GetPeerVersion(ovt1), 0u);

	Json::Value no_version;
	no_version["ovt"]["agent"] = "x";
	EXPECT_EQ(ovt::GetPeerVersion(no_version), 0u);

	Json::Value string_version;
	string_version["ovt"]["version"] = "3";
	EXPECT_EQ(ovt::GetPeerVersion(string_version), 0u);

	Json::Value huge;
	huge["ovt"]["version"] = Json::Int64(4294967296LL);
	EXPECT_EQ(ovt::GetPeerVersion(huge), 0u);

	// A generation above this build is read as it is. Holding back is the sender's call,
	// and no value of this field refuses a peer.
	Json::Value future;
	future["ovt"]["version"] = 99;
	EXPECT_EQ(ovt::GetPeerVersion(future), 99u);

	Json::Value current;
	current["ovt"]["version"] = ovt::OVT_PROTOCOL_VERSION;
	EXPECT_EQ(ovt::GetPeerVersion(current), ovt::OVT_PROTOCOL_VERSION);
}

// The `ovt` slot holds an object or nothing. A value of another type is a peer that speaks
// neither generation, and both directions refuse the message instead of reading it as OVT1.
TEST(OvtSignalingTest, NonObjectOvtIsRefused)
{
	Json::Value absent;
	absent["application"] = "describe";
	EXPECT_FALSE(ovt::HasNonObjectOvt(absent));

	Json::Value null_slot;
	null_slot["ovt"] = Json::Value();
	EXPECT_FALSE(ovt::HasNonObjectOvt(null_slot));
	EXPECT_FALSE(ovt::HasOvtObject(null_slot));

	Json::Value object_slot;
	object_slot["ovt"]["version"] = 2;
	EXPECT_FALSE(ovt::HasNonObjectOvt(object_slot));

	// A root that is not an object never reaches the slot rule: it fails the message checks
	Json::Value array(Json::arrayValue);
	EXPECT_FALSE(ovt::HasNonObjectOvt(array));

	for (const auto &slot : {Json::Value("2"), Json::Value(2), Json::Value(true), Json::Value(Json::arrayValue)})
	{
		Json::Value root;
		root["ovt"] = slot;
		EXPECT_TRUE(ovt::HasNonObjectOvt(root));
		EXPECT_FALSE(ovt::HasOvtObject(root));
	}
}

// The verdict an edge reaches on an origin response. This is the judgment itself, not a comparison
// that resembles it: `ReceiveDescribe()` and `ReceivePlay()` both map these to a log line and a return.
TEST(OvtSignalingTest, OriginResponseVerdicts)
{
	Json::Value ovt1;
	ovt1["code"] = 200;
	EXPECT_EQ(ovt::JudgeOriginResponse(ovt1), ovt::OriginVerdict::Ovt1);

	Json::Value null_ovt;
	null_ovt["ovt"] = Json::Value::nullSingleton();
	EXPECT_EQ(ovt::JudgeOriginResponse(null_ovt), ovt::OriginVerdict::Ovt1);

	Json::Value not_an_object;
	not_an_object["ovt"] = 2;
	EXPECT_EQ(ovt::JudgeOriginResponse(not_an_object), ovt::OriginVerdict::NotAnObject);

	Json::Value no_layout;
	no_layout["ovt"]["agent"] = "x";
	EXPECT_EQ(ovt::JudgeOriginResponse(no_layout), ovt::OriginVerdict::NoMediaHeaderVersion);

	Json::Value bad_layout;
	bad_layout["ovt"]["mediaHeaderVersion"] = "1";
	EXPECT_EQ(ovt::JudgeOriginResponse(bad_layout), ovt::OriginVerdict::NoMediaHeaderVersion);

	Json::Value current;
	current["ovt"]["mediaHeaderVersion"] = ovt::OVT_MEDIA_HEADER_VERSION;
	EXPECT_EQ(ovt::JudgeOriginResponse(current), ovt::OriginVerdict::Ovt2);

	// Above what this build reads: refused
	Json::Value future;
	future["ovt"]["mediaHeaderVersion"] = ovt::OVT_MEDIA_HEADER_VERSION + 1;
	EXPECT_EQ(ovt::JudgeOriginResponse(future), ovt::OriginVerdict::MediaHeaderTooNew);

	// Below: read, because a build knows every layout up to its own
	Json::Value older;
	older["ovt"]["mediaHeaderVersion"] = ovt::OVT_MEDIA_HEADER_VERSION;
	EXPECT_EQ(ovt::JudgeOriginResponse(older, ovt::OVT_MEDIA_HEADER_VERSION + 5), ovt::OriginVerdict::Ovt2);

	// The origin's generation never decides the verdict, whatever it says
	Json::Value far_ahead;
	far_ahead["ovt"]["mediaHeaderVersion"] = ovt::OVT_MEDIA_HEADER_VERSION;
	far_ahead["ovt"]["version"]			   = 9999;
	EXPECT_EQ(ovt::JudgeOriginResponse(far_ahead), ovt::OriginVerdict::Ovt2);
}

// A failure response echoes peer-chosen strings, so both echoed fields are cut at 256 bytes
TEST(OvtSignalingTest, TruncateForResponse)
{
	EXPECT_EQ(ovt::TruncateForResponse("").GetLength(), 0u);
	EXPECT_STREQ(ovt::TruncateForResponse("play").CStr(), "play");

	// Exactly at the limit passes through unchanged
	ov::String exact(std::string(ovt::MAX_RESPONSE_FIELD_LENGTH, 'a').c_str());
	EXPECT_EQ(ovt::TruncateForResponse(exact).GetLength(), ovt::MAX_RESPONSE_FIELD_LENGTH);
	EXPECT_STREQ(ovt::TruncateForResponse(exact).CStr(), exact.CStr());

	// One byte over is cut and marked
	ov::String over(std::string(ovt::MAX_RESPONSE_FIELD_LENGTH + 1, 'a').c_str());
	auto cut = ovt::TruncateForResponse(over);
	EXPECT_EQ(cut.GetLength(), ovt::MAX_RESPONSE_FIELD_LENGTH + 3);
	EXPECT_TRUE(cut.HasSuffix("..."));

	// A million characters costs the same 256 bytes
	ov::String huge(std::string(1000000, 'b').c_str());
	EXPECT_EQ(ovt::TruncateForResponse(huge).GetLength(), ovt::MAX_RESPONSE_FIELD_LENGTH + 3);
}

// Both objects carry `agent` and the sender's generation; only the response declares a media
// header layout, because an edge sends no media
TEST(OvtSignalingTest, OvtObjects)
{
	auto request = ovt::MakeRequestOvtObject();
	ASSERT_TRUE(request.isObject());
	EXPECT_EQ(request.size(), 2u);
	ASSERT_TRUE(request["agent"].isString());
	EXPECT_TRUE(ov::String(request["agent"].asString().c_str()).HasPrefix("OvenMediaEngine/"));
	EXPECT_EQ(request["version"].asUInt(), ovt::OVT_PROTOCOL_VERSION);
	EXPECT_FALSE(request.isMember("mediaHeaderVersion"));

	auto response = ovt::MakeResponseOvtObject();
	EXPECT_EQ(response.size(), 3u);
	EXPECT_EQ(response["agent"], request["agent"]);
	EXPECT_EQ(response["version"].asUInt(), ovt::OVT_PROTOCOL_VERSION);
	EXPECT_EQ(response["mediaHeaderVersion"].asUInt(), ovt::OVT_MEDIA_HEADER_VERSION);
}

// The agent string is "OvenMediaEngine/<version> (<git describe>)"; nothing decides on it, but it must be
// stable enough for an operator to read
TEST(OvtSignalingTest, AgentFormat)
{
	auto agent = ovt::GetOvtAgent();
	EXPECT_TRUE(agent.HasPrefix("OvenMediaEngine/"));
	auto rest = agent.Substring(strlen("OvenMediaEngine/"));

	// The release number is always there. What follows it comes from `git describe` and is empty in a
	// build made without a `.git` directory, so its shape is not pinned here.
	EXPECT_GT(rest.IndexOf('.'), 0);
	EXPECT_FALSE(rest.IsEmpty());

	// jsoncpp writes keys in byte order, so the request lays out as `agent` then `version`
	auto request = ov::Json::Stringify(ovt::MakeRequestOvtObject());
	EXPECT_TRUE(request.HasPrefix("{\"agent\":\""));
	EXPECT_NE(request.IndexOf("\"version\":"), -1) << request.CStr();
}

// The supported set is exactly the enumerators of the three wire tables plus the five features.
// Only the count and each token's shape are checked here; the tokens themselves are pinned by the
// baseline tables in `ovt_wire_test.cpp`.
TEST(OvtSignalingTest, RequiredTokenSetSize)
{
	const auto &tokens = ovt::SupportedRequiredTokens();
	EXPECT_EQ(tokens.size(), 17u + 29u + 7u + 5u);

	// Every token has a category prefix and no whitespace
	for (const auto &token : tokens)
	{
		EXPECT_GT(token.IndexOf('/'), 0) << token.CStr();
		EXPECT_LT(token.IndexOf(' '), 0) << token.CStr();
	}

	EXPECT_FALSE(ovt::IsSupportedRequiredToken("codec/H264 "));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken(" codec/H264"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken("codec/"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken("bitstream/Unknown"));
	EXPECT_FALSE(ovt::IsSupportedRequiredToken("packettype/Unknown"));
}

TEST(OvtSignalingTest, RequiredPayloadEmptyAndOrder)
{
	EXPECT_STREQ(ovt::MakeRequiredPayload({}).CStr(), "{\"required\":[]}");
	// Order is preserved as given (the caller sorts)
	EXPECT_STREQ(ovt::MakeRequiredPayload({"codec/H264", "codec/AAC"}).CStr(), "{\"required\":[\"codec/H264\",\"codec/AAC\"]}");
}

// The empty track set is a 200 for OVT1 compatibility, so the token is the whole signal
TEST(OvtSignalingTest, EmptyTrackSetReasonToken)
{
	EXPECT_STREQ(ovt::REASON_NO_DELIVERABLE_TRACK, "no-deliverable-track");
}

// `ovt.agent` is a string the peer chooses and a message may carry up to MAX_MESSAGE_BUFFER_SIZE
// of it. It is kept for the life of the connection and written to a log line, so it is cut to a
// fixed length and control characters are removed before either happens.
TEST(OvtSignalingTest, AgentIsCutToTheLimit)
{
	EXPECT_EQ(ovt::SanitizeForLog("").GetLength(), 0u);
	EXPECT_STREQ(ovt::SanitizeForLog("OvenMediaEngine/0.21.0").CStr(), "OvenMediaEngine/0.21.0");

	// Exactly at the limit is kept whole
	ov::String exact(std::string(ovt::MAX_LOG_STRING_LENGTH, 'a').c_str());
	EXPECT_EQ(ovt::SanitizeForLog(exact).GetLength(), ovt::MAX_LOG_STRING_LENGTH);

	// One over, and far over, are both cut to the limit
	ov::String over(std::string(ovt::MAX_LOG_STRING_LENGTH + 1, 'b').c_str());
	EXPECT_EQ(ovt::SanitizeForLog(over).GetLength(), ovt::MAX_LOG_STRING_LENGTH);

	ov::String huge(std::string(64 * 1024, 'c').c_str());
	EXPECT_EQ(ovt::SanitizeForLog(huge).GetLength(), ovt::MAX_LOG_STRING_LENGTH);

	// The cut is by byte, not by character, so it can land inside a multi-byte sequence.
	// What it must not do is read past the end or return more bytes than the limit.
	// `MAX_LOG_STRING_LENGTH` is not a multiple of 3, so the Hangul below straddles it.
	std::string hangul;
	while (hangul.size() < ovt::MAX_LOG_STRING_LENGTH + 16)
	{
		hangul += "\xed\x95\x9c";  // U+D55C
	}
	auto cut = ovt::SanitizeForLog(ov::String(hangul.c_str()));
	EXPECT_EQ(cut.GetLength(), ovt::MAX_LOG_STRING_LENGTH);

	// A control byte after the cut point is gone with the rest of the tail
	std::string with_tail(ovt::MAX_LOG_STRING_LENGTH, 'a');
	with_tail += "\n[FAKE]";
	EXPECT_EQ(ovt::SanitizeForLog(ov::String(with_tail.c_str())).GetLength(), ovt::MAX_LOG_STRING_LENGTH);
	EXPECT_EQ(ovt::SanitizeForLog(ov::String(with_tail.c_str())).IndexOf('.'), -1);
}

TEST(OvtSignalingTest, AgentCannotForgeALogLine)
{
	// A newline would otherwise let the peer write what looks like another log entry
	EXPECT_STREQ(ovt::SanitizeForLog("real\n[FAKE] injected").CStr(), "real.[FAKE] injected");
	EXPECT_STREQ(ovt::SanitizeForLog("a\r\nb").CStr(), "a..b");
	EXPECT_STREQ(ovt::SanitizeForLog("tab\there").CStr(), "tab.here");

	// The other end of the control range, and the escape that drives a terminal
	EXPECT_STREQ(ovt::SanitizeForLog("bell\x07").CStr(), "bell.");
	EXPECT_STREQ(ovt::SanitizeForLog("esc\x1b[31mred").CStr(), "esc.[31mred");
	EXPECT_STREQ(ovt::SanitizeForLog("del\x7f").CStr(), "del.");

	// Bytes above the control range are left alone: a UTF-8 agent stays readable
	EXPECT_STREQ(ovt::SanitizeForLog("OME/\xed\x95\x9c").CStr(), "OME/\xed\x95\x9c");
}
