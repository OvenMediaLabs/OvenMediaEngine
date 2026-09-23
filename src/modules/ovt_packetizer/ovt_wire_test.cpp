//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "ovt_wire.h"

#include <base/info/media_track.h>
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "ovt_depacketizer.h"
#include "ovt_packetizer.h"
#include "ovt_signaling.h"

// Regression baseline for the OVT wire table.
// Every row is a hand-written literal taken from the values the released builds put on the wire
// (v0.21.0.0 is the latest reference);
// none of it is generated from `ToOvtWire()` or from `Get*String()`,
// because a table generated from the code under test passes
// even when the whole table has shifted.
// Editing an existing row is a protocol change.
// Adding an enumerator adds one row.
//
// OVT1 wrote the header bytes and the describe integers through `static_cast<int8_t>`,
// so every enumerator whose value is -1 is 0xFF on the wire.

namespace
{
	constexpr uint8_t WIRE_NO_VALUE = 0xFF;

	template <typename T>
	struct WireRow
	{
		T value;
		const char *name;
		uint8_t wire;
	};

	// 17 rows
	const std::vector<WireRow<cmn::MediaCodecId>> CODEC_ID_ROWS = {
		{cmn::MediaCodecId::None, "None", 0},
		{cmn::MediaCodecId::H264, "H264", 1},
		{cmn::MediaCodecId::H265, "H265", 2},
		{cmn::MediaCodecId::Vp8, "VP8", 3},
		{cmn::MediaCodecId::Vp9, "VP9", 4},
		{cmn::MediaCodecId::Av1, "AV1", 5},
		{cmn::MediaCodecId::Flv, "FLV", 6},
		{cmn::MediaCodecId::Aac, "AAC", 7},
		{cmn::MediaCodecId::Mp3, "MP3", 8},
		{cmn::MediaCodecId::Opus, "OPUS", 9},
		{cmn::MediaCodecId::Jpeg, "JPEG", 10},
		{cmn::MediaCodecId::Png, "PNG", 11},
		{cmn::MediaCodecId::Webp, "WEBP", 12},
		{cmn::MediaCodecId::WebVTT, "WebVTT", 13},
		{cmn::MediaCodecId::Whisper, "WHISPER", 14},
		{cmn::MediaCodecId::Mp2, "MP2", 15},
		{cmn::MediaCodecId::Avif, "AVIF", 16},
	};

	// 30 rows
	const std::vector<WireRow<cmn::BitstreamFormat>> BITSTREAM_FORMAT_ROWS = {
		{cmn::BitstreamFormat::Unknown, "Unknown", WIRE_NO_VALUE},
		{cmn::BitstreamFormat::H264_AVCC, "AVCC", 0},
		{cmn::BitstreamFormat::H264_ANNEXB, "H264_ANNEXB", 1},
		{cmn::BitstreamFormat::H264_RTP_RFC_6184, "H264_RTP_RFC_6184", 2},
		{cmn::BitstreamFormat::HVCC, "HVCC", 3},
		{cmn::BitstreamFormat::H265_ANNEXB, "H265_ANNEXB", 4},
		{cmn::BitstreamFormat::H265_RTP_RFC_7798, "H265_RTP_RFC_7798", 5},
		{cmn::BitstreamFormat::VP8, "VP8", 6},
		{cmn::BitstreamFormat::VP8_RTP_RFC_7741, "VP8_RTP_RFC_7741", 7},
		{cmn::BitstreamFormat::AAC_RAW, "AAC_RAW", 8},
		{cmn::BitstreamFormat::AAC_MPEG4_GENERIC, "AAC_MPEG4_GENERIC", 9},
		{cmn::BitstreamFormat::AAC_ADTS, "AAC_ADTS", 10},
		{cmn::BitstreamFormat::AAC_LATM, "AAC_LATM", 11},
		{cmn::BitstreamFormat::OPUS, "OPUS", 12},
		{cmn::BitstreamFormat::OPUS_RTP_RFC_7587, "OPUS_RTP_RFC_7587", 13},
		{cmn::BitstreamFormat::MP3, "MP3", 14},
		{cmn::BitstreamFormat::JPEG, "JPEG", 15},
		{cmn::BitstreamFormat::PNG, "PNG", 16},
		{cmn::BitstreamFormat::WEBP, "WEBP", 17},
		{cmn::BitstreamFormat::ID3v2, "ID3v2", 18},
		{cmn::BitstreamFormat::OVEN_EVENT, "OVEN_EVENT", 19},
		{cmn::BitstreamFormat::CUE, "CUE", 20},
		{cmn::BitstreamFormat::AMF, "AMF", 21},
		{cmn::BitstreamFormat::SEI, "SEI", 22},
		{cmn::BitstreamFormat::SCTE35, "SCTE35", 23},
		{cmn::BitstreamFormat::WebVTT, "WebVTT", 24},
		{cmn::BitstreamFormat::MP2, "MP2", 25},
		{cmn::BitstreamFormat::AV1_OBU, "AV1_OBU", 26},
		{cmn::BitstreamFormat::AV1_RTP_AOM, "AV1_RTP_AOM", 27},
		{cmn::BitstreamFormat::AVIF, "AVIF", 28},
	};

	// 11 rows. Nb is an end marker and never on the wire.
	const std::vector<WireRow<cmn::AudioSample::Format>> SAMPLE_FORMAT_ROWS = {
		{cmn::AudioSample::Format::None, "none", WIRE_NO_VALUE},
		{cmn::AudioSample::Format::U8, "u8", 0},
		{cmn::AudioSample::Format::S16, "s16", 1},
		{cmn::AudioSample::Format::S32, "s32", 2},
		{cmn::AudioSample::Format::Flt, "flt", 3},
		{cmn::AudioSample::Format::Dbl, "dbl", 4},
		{cmn::AudioSample::Format::U8P, "u8p", 5},
		{cmn::AudioSample::Format::S16P, "s16p", 6},
		{cmn::AudioSample::Format::S32P, "s32p", 7},
		{cmn::AudioSample::Format::FltP, "fltp", 8},
		{cmn::AudioSample::Format::DblP, "dblp", 9},
	};

	// 8 rows
	const std::vector<WireRow<cmn::PacketType>> PACKET_TYPE_ROWS = {
		{cmn::PacketType::Unknown, "Unknown", WIRE_NO_VALUE},
		{cmn::PacketType::OVT, "OVT", 0},
		{cmn::PacketType::RAW, "RAW", 1},
		{cmn::PacketType::SEQUENCE_HEADER, "SEQUENCE_HEADER", 2},
		{cmn::PacketType::NALU, "NALU", 3},
		{cmn::PacketType::EVENT, "EVENT", 4},
		{cmn::PacketType::VIDEO_EVENT, "VIDEO_EVENT", 5},
		{cmn::PacketType::AUDIO_EVENT, "AUDIO_EVENT", 6},
	};

	// 6 rows. Nb is an end marker and never on the wire.
	const std::vector<WireRow<cmn::MediaType>> MEDIA_TYPE_ROWS = {
		{cmn::MediaType::Unknown, "Unknown", WIRE_NO_VALUE},
		{cmn::MediaType::Video, "Video", 0},
		{cmn::MediaType::Audio, "Audio", 1},
		{cmn::MediaType::Data, "Data", 2},
		{cmn::MediaType::Subtitle, "Subtitle", 3},
		{cmn::MediaType::Attachment, "Attachment", 4},
	};

	// 3 rows
	const std::vector<WireRow<MediaPacketFlag>> MEDIA_PACKET_FLAG_ROWS = {
		{MediaPacketFlag::Unknown, "Unknown", 0},
		{MediaPacketFlag::NoFlag, "NoFlag", 1},
		{MediaPacketFlag::Key, "Key", 2},
	};

	template <typename T>
	void ExpectRows(const std::vector<WireRow<T>> &rows)
	{
		for (const auto &row : rows)
		{
			EXPECT_EQ(ovt::ToOvtWire(row.value), row.wire) << row.name;
			ASSERT_TRUE(ovt::FromOvtWire<T>(row.wire).has_value()) << row.name;
			EXPECT_EQ(*ovt::FromOvtWire<T>(row.wire), row.value) << row.name;
		}
	}

	// Every wire byte outside the rows must be rejected by `FromOvtWire()`,
	// so that no byte a future release assigns can alias an existing enumerator.
	template <typename T>
	void ExpectOnlyRowsAreMapped(const std::vector<WireRow<T>> &rows)
	{
		for (int wire = 0; wire <= UINT8_MAX; wire++)
		{
			auto in_rows = false;
			for (const auto &row : rows)
			{
				in_rows = in_rows || (row.wire == wire);
			}

			EXPECT_EQ(ovt::FromOvtWire<T>(static_cast<uint8_t>(wire)).has_value(), in_rows) << "wire " << wire;
		}
	}

	// Returns an empty vector when packetizing fails, which no expectation below accepts
	std::vector<uint8_t> PacketizeHeader(const std::shared_ptr<MediaPacket> &media_packet)
	{
		OvtPacketizer packetizer;
		if (packetizer.PacketizeMediaPacket(0, media_packet) == false)
		{
			return {};
		}

		auto packet = packetizer.PopPacket();
		if ((packet == nullptr) || (packet->PayloadLength() < MEDIA_PACKET_HEADER_SIZE))
		{
			return {};
		}

		return std::vector<uint8_t>(packet->Payload(), packet->Payload() + MEDIA_PACKET_HEADER_SIZE);
	}

	std::shared_ptr<MediaPacket> Depacketize(const std::vector<uint8_t> &media_payload)
	{
		OvtPacket packet;
		packet.SetSessionId(0);
		packet.SetSequenceNumber(0);
		packet.SetPayloadType(OvtPayloadType::MediaPacket);
		packet.SetTimestamp(0);
		packet.SetMarker(true);
		packet.SetPayload(media_payload.data(), media_payload.size());

		OvtDepacketizer depacketizer;
		if (depacketizer.AppendPacket(packet.GetData()) == false)
		{
			return nullptr;
		}

		return depacketizer.PopMediaPacket();
	}

	// 36-byte media packet header as v0.21.0.0 writes it, followed by `data`
	std::vector<uint8_t> MakeMediaPayload(uint32_t track_id, uint8_t media_type, uint8_t flag, uint8_t bitstream_format, uint8_t packet_type, const std::vector<uint8_t> &data)
	{
		std::vector<uint8_t> payload(MEDIA_PACKET_HEADER_SIZE, 0);
		payload[3]	= static_cast<uint8_t>(track_id);
		payload[28] = media_type;
		payload[29] = flag;
		payload[30] = bitstream_format;
		payload[31] = packet_type;
		payload[35] = static_cast<uint8_t>(data.size());
		payload.insert(payload.end(), data.begin(), data.end());
		return payload;
	}
}  // namespace

// The baseline tables are written by hand, so nothing ties them to the enums on its own.
// Without this, adding an enumerator leaves every other test green while its row is missing:
// `-Werror=switch` forces a wire value into `ovt_wire.cpp` but says nothing about this file.
TEST(OvtWireTest, EveryEnumeratorHasABaselineRow)
{
	EXPECT_EQ(CODEC_ID_ROWS.size(), cmn::ALL_MEDIA_CODEC_IDS.size())
		<< "a codec was added; give it a row here and a wire value in ovt_wire.cpp";
	EXPECT_EQ(BITSTREAM_FORMAT_ROWS.size(), cmn::ALL_BITSTREAM_FORMATS.size() + 1)
		<< "a bitstream format was added; the +1 is `Unknown`, which the table carries and the list does not";
	EXPECT_EQ(PACKET_TYPE_ROWS.size(), cmn::ALL_PACKET_TYPES.size() + 1)
		<< "a packet type was added; the +1 is `Unknown`";
	EXPECT_EQ(SAMPLE_FORMAT_ROWS.size(), static_cast<size_t>(cmn::AudioSample::Format::Nb) + 1)
		<< "a sample format was added; the +1 is `None`, which sorts before the table's first index";
}

TEST(OvtWireTest, BaselineRowsRoundTrip)
{
	ExpectRows(CODEC_ID_ROWS);
	ExpectRows(BITSTREAM_FORMAT_ROWS);
	ExpectRows(SAMPLE_FORMAT_ROWS);
	ExpectRows(PACKET_TYPE_ROWS);
	ExpectRows(MEDIA_TYPE_ROWS);
	ExpectRows(MEDIA_PACKET_FLAG_ROWS);
}

// Every codec named here shipped with or before v0.21.0.0, so an OVT1 peer knows all of them.
// The list is written out rather than taken from `CODEC_ID_ROWS`: a codec added after that release
// must answer false, and looping over every row would force the opposite on it.
TEST(OvtWireTest, EveryCodecOfTheLastOvt1ReleaseHasALegacyWireValue)
{
	const cmn::MediaCodecId shipped_by_v0_21_0[] = {
		cmn::MediaCodecId::None,
		cmn::MediaCodecId::H264,
		cmn::MediaCodecId::H265,
		cmn::MediaCodecId::Vp8,
		cmn::MediaCodecId::Vp9,
		cmn::MediaCodecId::Flv,
		cmn::MediaCodecId::Aac,
		cmn::MediaCodecId::Mp3,
		cmn::MediaCodecId::Opus,
		cmn::MediaCodecId::Jpeg,
		cmn::MediaCodecId::Png,
		cmn::MediaCodecId::Webp,
		cmn::MediaCodecId::WebVTT,
		cmn::MediaCodecId::Whisper,
		cmn::MediaCodecId::Mp2,
		cmn::MediaCodecId::Av1,
		cmn::MediaCodecId::Avif,
	};

	for (auto codec_id : shipped_by_v0_21_0)
	{
		EXPECT_TRUE(ovt::HasLegacyWireValue(codec_id)) << cmn::GetCodecIdString(codec_id);
	}

	// The list is the whole enum today. When it stops being so, the difference is a new codec
	// and `HasLegacyWireValue()` must say false for it.
	EXPECT_EQ(std::size(shipped_by_v0_21_0), cmn::ALL_MEDIA_CODEC_IDS.size());
}

TEST(OvtWireTest, BytesOutsideBaselineAreUnmapped)
{
	ExpectOnlyRowsAreMapped(CODEC_ID_ROWS);
	ExpectOnlyRowsAreMapped(BITSTREAM_FORMAT_ROWS);
	ExpectOnlyRowsAreMapped(SAMPLE_FORMAT_ROWS);
	ExpectOnlyRowsAreMapped(PACKET_TYPE_ROWS);
	ExpectOnlyRowsAreMapped(MEDIA_TYPE_ROWS);
}

// `MH[29]` is the only bitmask byte, so it does not follow the rule above:
// a byte with a known bit keeps that meaning and the unknown bits in it are dropped.
// `0x00` is `Unknown`'s own value, so only a non-zero byte with no known bit stays unmapped,
// which is what preserves it for a relay.
TEST(OvtWireTest, FlagByteDropsUnknownBits)
{
	EXPECT_EQ(ovt::FromOvtWire<MediaPacketFlag>(0b00), MediaPacketFlag::Unknown);
	EXPECT_EQ(ovt::FromOvtWire<MediaPacketFlag>(0b01), MediaPacketFlag::NoFlag);
	EXPECT_EQ(ovt::FromOvtWire<MediaPacketFlag>(0b10), MediaPacketFlag::Key);

	// Both known bits at once: a key frame is the meaning worth keeping
	EXPECT_EQ(ovt::FromOvtWire<MediaPacketFlag>(0b11), MediaPacketFlag::Key);

	// A flag this build does not know, sent alongside one it does
	EXPECT_EQ(ovt::FromOvtWire<MediaPacketFlag>(0b110), MediaPacketFlag::Key);
	EXPECT_EQ(ovt::FromOvtWire<MediaPacketFlag>(0b101), MediaPacketFlag::NoFlag);
	EXPECT_EQ(ovt::FromOvtWire<MediaPacketFlag>(0xFF), MediaPacketFlag::Key);

	// Nothing this build knows: unmapped, so the byte is preserved as received
	for (int wire = 0; wire <= UINT8_MAX; wire++)
	{
		if ((wire & 0b11) != 0)
		{
			continue;
		}

		EXPECT_EQ(ovt::FromOvtWire<MediaPacketFlag>(static_cast<uint8_t>(wire)).has_value(), wire == 0) << "wire " << wire;
	}
}

// The baseline names are the protocol names:
// the forward function emits them and the reverse function reads them back,
// so a rename or a reverse entry left out fails here.
TEST(OvtWireTest, BaselineNamesRoundTripThroughNameFunctions)
{
	for (const auto &row : CODEC_ID_ROWS)
	{
		EXPECT_STREQ(cmn::GetCodecIdString(row.value), row.name);
		EXPECT_EQ(cmn::GetCodecIdByExactName(row.name), row.value) << row.name;
	}

	for (const auto &row : BITSTREAM_FORMAT_ROWS)
	{
		EXPECT_STREQ(cmn::GetBitstreamFormatString(row.value), row.name);
		EXPECT_EQ(cmn::GetBitstreamFormatByName(row.name), row.value) << row.name;
	}

	for (const auto &row : SAMPLE_FORMAT_ROWS)
	{
		EXPECT_STREQ(cmn::AudioSample(row.value).GetName(), row.name);
		EXPECT_EQ(cmn::GetAudioSampleFormatByName(row.name), row.value) << row.name;
	}

	for (const auto &row : PACKET_TYPE_ROWS)
	{
		EXPECT_STREQ(cmn::GetPacketTypeString(row.value), row.name);
		EXPECT_EQ(cmn::GetPacketTypeByName(row.name), row.value) << row.name;
	}

	for (const auto &row : MEDIA_TYPE_ROWS)
	{
		EXPECT_STREQ(cmn::GetMediaTypeString(row.value), row.name);
		EXPECT_EQ(cmn::GetMediaTypeByName(row.name), row.value) << row.name;
	}

	for (const auto &row : MEDIA_PACKET_FLAG_ROWS)
	{
		EXPECT_STREQ(GetMediaPacketFlagString(row.value).CStr(), row.name);
	}
}

// Reverse lookups are exact: no case folding, no prefix matching, and no end markers
TEST(OvtWireTest, ReverseNameLookupIsExact)
{
	EXPECT_FALSE(cmn::GetCodecIdByExactName("h264").has_value());
	EXPECT_FALSE(cmn::GetCodecIdByExactName("H264_ANNEXB").has_value());
	EXPECT_FALSE(cmn::GetCodecIdByExactName("").has_value());
	EXPECT_FALSE(cmn::GetBitstreamFormatByName("H264_AVCC").has_value());
	EXPECT_FALSE(cmn::GetBitstreamFormatByName("avcc").has_value());
	EXPECT_FALSE(cmn::GetAudioSampleFormatByName("FLTP").has_value());
	EXPECT_FALSE(cmn::GetAudioSampleFormatByName("Nb").has_value());
	EXPECT_FALSE(cmn::GetPacketTypeByName("nalu").has_value());
	EXPECT_FALSE(cmn::GetMediaTypeByName("Nb").has_value());
	EXPECT_FALSE(cmn::GetMediaTypeByName("video").has_value());
}

// Values cast in from outside the enumerator range collapse to `None`, so "none" is only ever the `None` format
TEST(OvtWireTest, AudioSampleFormatOutOfRangeIsNone)
{
	for (int raw : {10, 11, 127, -2, -128})
	{
		cmn::AudioSample sample(static_cast<cmn::AudioSample::Format>(raw));
		EXPECT_EQ(sample.GetFormat(), cmn::AudioSample::Format::None) << raw;
		EXPECT_STREQ(sample.GetName(), "none") << raw;
	}
}

// Transport format per codec, one row per enumerator. A row is fixed once the codec has shipped.
TEST(OvtWireTest, TransportBitstreamFormatRows)
{
	struct Row
	{
		cmn::MediaCodecId codec_id;
		std::optional<cmn::BitstreamFormat> format;
	};

	const std::vector<Row> rows = {
		{cmn::MediaCodecId::None, std::nullopt},
		{cmn::MediaCodecId::H264, cmn::BitstreamFormat::H264_ANNEXB},
		{cmn::MediaCodecId::H265, cmn::BitstreamFormat::H265_ANNEXB},
		{cmn::MediaCodecId::Vp8, cmn::BitstreamFormat::VP8},
		{cmn::MediaCodecId::Vp9, std::nullopt},
		{cmn::MediaCodecId::Av1, cmn::BitstreamFormat::AV1_OBU},
		{cmn::MediaCodecId::Flv, std::nullopt},
		{cmn::MediaCodecId::Aac, cmn::BitstreamFormat::AAC_ADTS},
		{cmn::MediaCodecId::Mp3, cmn::BitstreamFormat::MP3},
		{cmn::MediaCodecId::Opus, cmn::BitstreamFormat::OPUS},
		{cmn::MediaCodecId::Jpeg, cmn::BitstreamFormat::JPEG},
		{cmn::MediaCodecId::Png, cmn::BitstreamFormat::PNG},
		{cmn::MediaCodecId::Webp, cmn::BitstreamFormat::WEBP},
		{cmn::MediaCodecId::WebVTT, cmn::BitstreamFormat::WebVTT},
		{cmn::MediaCodecId::Whisper, std::nullopt},
		{cmn::MediaCodecId::Mp2, cmn::BitstreamFormat::MP2},
		{cmn::MediaCodecId::Avif, cmn::BitstreamFormat::AVIF},
	};
	ASSERT_EQ(rows.size(), 17u);

	for (const auto &row : rows)
	{
		EXPECT_EQ(ovt::TransportBitstreamFormat(row.codec_id), row.format) << cmn::GetCodecIdString(row.codec_id);
	}
}

// Describe integers: OVT1 wrote them through `int8_t`, so both spellings of a byte must map,
// and anything outside both ranges is unmapped.
TEST(OvtWireTest, DescribeIntegerSpellings)
{
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::AudioSample::Format>(-1), cmn::AudioSample::Format::None);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::AudioSample::Format>(255), cmn::AudioSample::Format::None);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::AudioSample::Format>(8), cmn::AudioSample::Format::FltP);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaCodecId>(7), cmn::MediaCodecId::Aac);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaType>(-1), cmn::MediaType::Unknown);

	EXPECT_FALSE(ovt::FromOvtWireInt<cmn::MediaCodecId>(17).has_value());
	EXPECT_FALSE(ovt::FromOvtWireInt<cmn::MediaCodecId>(256).has_value());
	EXPECT_FALSE(ovt::FromOvtWireInt<cmn::MediaCodecId>(-129).has_value());
	EXPECT_FALSE(ovt::FromOvtWireInt<cmn::MediaCodecId>(INT64_MIN).has_value());
	EXPECT_FALSE(ovt::FromOvtWireInt<cmn::MediaCodecId>(INT64_MAX).has_value());
}

// Header bytes as v0.21.0.0 writes them for the packet kinds an Origin actually sends.
// The literal bytes are the expectation; the packetizer output must match them byte for byte.
TEST(OvtWireTest, ReleaseHeaderBytes)
{
	struct Sample
	{
		const char *label;
		cmn::MediaType media_type;
		MediaPacketFlag flag;
		cmn::BitstreamFormat bitstream_format;
		cmn::PacketType packet_type;
		uint8_t wire_media_type;
		uint8_t wire_flag;
		uint8_t wire_bitstream_format;
		uint8_t wire_packet_type;
	};

	const Sample samples[] = {
		{"H264 AnnexB key frame", cmn::MediaType::Video, MediaPacketFlag::Key, cmn::BitstreamFormat::H264_ANNEXB, cmn::PacketType::NALU, 0x00, 0x02, 0x01, 0x03},
		{"H265 AnnexB frame", cmn::MediaType::Video, MediaPacketFlag::NoFlag, cmn::BitstreamFormat::H265_ANNEXB, cmn::PacketType::NALU, 0x00, 0x01, 0x04, 0x03},
		{"AAC ADTS frame", cmn::MediaType::Audio, MediaPacketFlag::NoFlag, cmn::BitstreamFormat::AAC_ADTS, cmn::PacketType::RAW, 0x01, 0x01, 0x0A, 0x01},
		{"Opus frame", cmn::MediaType::Audio, MediaPacketFlag::NoFlag, cmn::BitstreamFormat::OPUS, cmn::PacketType::RAW, 0x01, 0x01, 0x0C, 0x01},
		{"AV1 OBU key frame", cmn::MediaType::Video, MediaPacketFlag::Key, cmn::BitstreamFormat::AV1_OBU, cmn::PacketType::RAW, 0x00, 0x02, 0x1A, 0x01},
		{"AVIF image", cmn::MediaType::Video, MediaPacketFlag::Key, cmn::BitstreamFormat::AVIF, cmn::PacketType::RAW, 0x00, 0x02, 0x1C, 0x01},
		{"ID3v2 event", cmn::MediaType::Data, MediaPacketFlag::NoFlag, cmn::BitstreamFormat::ID3v2, cmn::PacketType::EVENT, 0x02, 0x01, 0x12, 0x04},
		{"WebVTT cue", cmn::MediaType::Subtitle, MediaPacketFlag::NoFlag, cmn::BitstreamFormat::WebVTT, cmn::PacketType::RAW, 0x03, 0x01, 0x18, 0x01},
		{"Unset enums", cmn::MediaType::Unknown, MediaPacketFlag::Unknown, cmn::BitstreamFormat::Unknown, cmn::PacketType::Unknown, 0xFF, 0x00, 0xFF, 0xFF},
	};

	const std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};

	for (const auto &sample : samples)
	{
		auto media_packet = std::make_shared<MediaPacket>(sample.media_type, 1, data.data(), data.size(), 0, 0, 0, sample.flag, sample.bitstream_format, sample.packet_type);

		auto header		  = PacketizeHeader(media_packet);
		ASSERT_EQ(header.size(), static_cast<size_t>(MEDIA_PACKET_HEADER_SIZE)) << sample.label;

		auto expected = MakeMediaPayload(1, sample.wire_media_type, sample.wire_flag, sample.wire_bitstream_format, sample.wire_packet_type, data);
		expected.resize(MEDIA_PACKET_HEADER_SIZE);
		EXPECT_EQ(header, expected) << sample.label;

		// The same bytes read back give the same enums
		auto received = Depacketize(MakeMediaPayload(1, sample.wire_media_type, sample.wire_flag, sample.wire_bitstream_format, sample.wire_packet_type, data));
		ASSERT_TRUE(received != nullptr) << sample.label;
		EXPECT_EQ(received->GetMediaType(), sample.media_type) << sample.label;
		EXPECT_EQ(received->GetFlag(), sample.flag) << sample.label;
		EXPECT_EQ(received->GetBitstreamFormat(), sample.bitstream_format) << sample.label;
		EXPECT_EQ(received->GetPacketType(), sample.packet_type) << sample.label;
		EXPECT_FALSE(received->GetUnmappedWireValue(MediaPacket::WireField::BitstreamFormat).has_value()) << sample.label;
	}
}

// A byte without a table entry leaves the enum `Unknown`, is preserved as received, survives a clone,
// and the packetizer writes it back out unchanged.
// This is the depacketizer and packetizer pair, not a relay: in the product a packet whose enum is
// `Unknown` is refused by MediaRouter (`mediarouter/mediarouter_nomalize.cpp:116`) and never reaches
// an outbound packetizer, and the OVT provider overwrites `MH[31]` on the way in. What is pinned here
// is that the two ends of the wire table agree, which is what the cross-check and the log rest on.
// The flag byte carries no known bit here, which is what leaves it unmapped (see `FlagByteDropsUnknownBits`).
TEST(OvtWireTest, UnmappedBytesSurviveThePacketizerRoundTrip)
{
	const std::vector<uint8_t> data = {0x01, 0x02};
	auto payload					= MakeMediaPayload(7, 0x07, 0x08, 0xC8, 0x7F, data);

	auto received					= Depacketize(payload);
	ASSERT_TRUE(received != nullptr);

	EXPECT_EQ(received->GetMediaType(), cmn::MediaType::Unknown);
	EXPECT_EQ(received->GetFlag(), MediaPacketFlag::Unknown);
	EXPECT_EQ(received->GetBitstreamFormat(), cmn::BitstreamFormat::Unknown);
	EXPECT_EQ(received->GetPacketType(), cmn::PacketType::Unknown);

	EXPECT_EQ(received->GetUnmappedWireValue(MediaPacket::WireField::MediaType), 0x07);
	EXPECT_EQ(received->GetUnmappedWireValue(MediaPacket::WireField::Flag), 0x08);
	EXPECT_EQ(received->GetUnmappedWireValue(MediaPacket::WireField::BitstreamFormat), 0xC8);
	EXPECT_EQ(received->GetUnmappedWireValue(MediaPacket::WireField::PacketType), 0x7F);

	auto relayed = received->ClonePacket();
	auto header	 = PacketizeHeader(relayed);
	EXPECT_EQ(header, std::vector<uint8_t>(payload.begin(), payload.begin() + MEDIA_PACKET_HEADER_SIZE));
}

// A value set by a later stage wins over the preserved byte,
// and a legitimately `Unknown` enum with nothing preserved goes out as the table's `Unknown` byte.
TEST(OvtWireTest, KnownValueOverridesPreservedByte)
{
	const std::vector<uint8_t> data = {0x01};
	auto received					= Depacketize(MakeMediaPayload(1, 0x00, 0x01, 0xC8, 0x01, data));
	ASSERT_TRUE(received != nullptr);
	ASSERT_EQ(received->GetBitstreamFormat(), cmn::BitstreamFormat::Unknown);

	received->SetBitstreamFormat(cmn::BitstreamFormat::AAC_ADTS);
	auto header = PacketizeHeader(received);
	EXPECT_EQ(header[30], 0x0A);

	received->SetBitstreamFormat(cmn::BitstreamFormat::Unknown);
	header = PacketizeHeader(received);
	EXPECT_EQ(header[30], 0xC8);

	auto fresh = std::make_shared<MediaPacket>(cmn::MediaType::Video, 1, data.data(), data.size(), 0, 0, 0, MediaPacketFlag::Key, cmn::BitstreamFormat::Unknown, cmn::PacketType::NALU);
	header	   = PacketizeHeader(fresh);
	EXPECT_EQ(header[30], 0xFF);
}

// Every enumerator of the baseline is a supported required token,
// so a codec added to the wire table without a token would be refused by an edge of the same build
TEST(OvtWireTest, EveryBaselineEnumeratorIsASupportedToken)
{
	for (const auto &row : CODEC_ID_ROWS)
	{
		EXPECT_TRUE(ovt::IsSupportedRequiredToken(ovt::CodecToken(row.value))) << row.name;
		EXPECT_EQ(ovt::CodecToken(row.value), ovt::CodecToken(ov::String(row.name))) << row.name;
	}
	for (const auto &row : BITSTREAM_FORMAT_ROWS)
	{
		if (row.value != cmn::BitstreamFormat::Unknown)
		{
			EXPECT_TRUE(ovt::IsSupportedRequiredToken(ovt::BitstreamToken(row.value))) << row.name;
		}
	}
	for (const auto &row : PACKET_TYPE_ROWS)
	{
		if (row.value != cmn::PacketType::Unknown)
		{
			EXPECT_TRUE(ovt::IsSupportedRequiredToken(ovt::PacketTypeToken(row.value))) << row.name;
		}
	}
}

// ---- Boundaries ----

// End markers are never mapped back, and the describe integer range is exactly int8 .. uint8
TEST(OvtWireTest, EndMarkersAndIntegerRange)
{
	EXPECT_EQ(ovt::ToOvtWire(cmn::MediaType::Nb), 5);
	EXPECT_EQ(ovt::ToOvtWire(cmn::AudioSample::Format::Nb), 10);
	EXPECT_FALSE(ovt::FromOvtWire<cmn::MediaType>(5).has_value());
	EXPECT_FALSE(ovt::FromOvtWire<cmn::AudioSample::Format>(10).has_value());

	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaType>(-128), std::nullopt);	 // 0x80 is not a media type
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaType>(127), std::nullopt);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaType>(128), std::nullopt);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaType>(255), cmn::MediaType::Unknown);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaType>(-1), cmn::MediaType::Unknown);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaType>(4), cmn::MediaType::Attachment);
	EXPECT_EQ(ovt::FromOvtWireInt<cmn::MediaType>(0), cmn::MediaType::Video);
}

// Every transport format and packet type shipped by v0.21.0.0, so the OVT1 removal never fires on
// one of them today. The lists are written out rather than taken from the row tables, because a
// enumerator added after `v0.21.0.0` gets a row too and `HasLegacyWireValue()` must answer false for it.
// The codec side is a separate table.
TEST(OvtWireTest, TransportFormatsOfTheLastOvt1ReleaseHaveALegacyWireValue)
{
	const cmn::BitstreamFormat formats_shipped_by_v0_21_0[] = {
		cmn::BitstreamFormat::Unknown,
		cmn::BitstreamFormat::H264_AVCC,
		cmn::BitstreamFormat::H264_ANNEXB,
		cmn::BitstreamFormat::H264_RTP_RFC_6184,
		cmn::BitstreamFormat::HVCC,
		cmn::BitstreamFormat::H265_ANNEXB,
		cmn::BitstreamFormat::H265_RTP_RFC_7798,
		cmn::BitstreamFormat::VP8,
		cmn::BitstreamFormat::VP8_RTP_RFC_7741,
		cmn::BitstreamFormat::AAC_RAW,
		cmn::BitstreamFormat::AAC_MPEG4_GENERIC,
		cmn::BitstreamFormat::AAC_ADTS,
		cmn::BitstreamFormat::AAC_LATM,
		cmn::BitstreamFormat::OPUS,
		cmn::BitstreamFormat::OPUS_RTP_RFC_7587,
		cmn::BitstreamFormat::MP3,
		cmn::BitstreamFormat::JPEG,
		cmn::BitstreamFormat::PNG,
		cmn::BitstreamFormat::WEBP,
		cmn::BitstreamFormat::ID3v2,
		cmn::BitstreamFormat::OVEN_EVENT,
		cmn::BitstreamFormat::CUE,
		cmn::BitstreamFormat::AMF,
		cmn::BitstreamFormat::SEI,
		cmn::BitstreamFormat::SCTE35,
		cmn::BitstreamFormat::WebVTT,
		cmn::BitstreamFormat::MP2,
		cmn::BitstreamFormat::AV1_OBU,
		cmn::BitstreamFormat::AV1_RTP_AOM,
		cmn::BitstreamFormat::AVIF,
	};

	const cmn::PacketType packet_types_shipped_by_v0_21_0[] = {
		cmn::PacketType::Unknown,
		cmn::PacketType::OVT,
		cmn::PacketType::RAW,
		cmn::PacketType::SEQUENCE_HEADER,
		cmn::PacketType::NALU,
		cmn::PacketType::EVENT,
		cmn::PacketType::VIDEO_EVENT,
		cmn::PacketType::AUDIO_EVENT,
	};

	for (auto format : formats_shipped_by_v0_21_0)
	{
		EXPECT_TRUE(ovt::HasLegacyWireValue(format)) << cmn::GetBitstreamFormatString(format);
	}
	for (auto packet_type : packet_types_shipped_by_v0_21_0)
	{
		EXPECT_TRUE(ovt::HasLegacyWireValue(packet_type)) << cmn::GetPacketTypeString(packet_type);
	}

	// Each list is the whole enum today, `Unknown` included; the `ALL_*` arrays leave that sentinel out,
	// which is the same `+ 1` `EveryEnumeratorHasABaselineRow` uses. When a list stops matching,
	// the difference is a new value and `HasLegacyWireValue()` must say false for it.
	EXPECT_EQ(std::size(formats_shipped_by_v0_21_0), cmn::ALL_BITSTREAM_FORMATS.size() + 1);
	EXPECT_EQ(std::size(packet_types_shipped_by_v0_21_0), cmn::ALL_PACKET_TYPES.size() + 1);
}

// Unmapped slots start empty, can be overwritten, and are per field
TEST(OvtWireTest, UnmappedWireSlots)
{
	const std::vector<uint8_t> data = {1};
	auto packet						= std::make_shared<MediaPacket>(cmn::MediaType::Video, 1, data.data(), data.size(), 0, 0, 0, MediaPacketFlag::Key, cmn::BitstreamFormat::H264_ANNEXB, cmn::PacketType::NALU);

	for (auto field : {MediaPacket::WireField::MediaType, MediaPacket::WireField::Flag, MediaPacket::WireField::BitstreamFormat, MediaPacket::WireField::PacketType})
	{
		EXPECT_FALSE(packet->GetUnmappedWireValue(field).has_value());
	}

	packet->SetUnmappedWireValue(MediaPacket::WireField::Flag, 9);
	packet->SetUnmappedWireValue(MediaPacket::WireField::Flag, 10);
	EXPECT_EQ(packet->GetUnmappedWireValue(MediaPacket::WireField::Flag), 10);
	EXPECT_FALSE(packet->GetUnmappedWireValue(MediaPacket::WireField::MediaType).has_value());

	auto clone = packet->ClonePacket();
	EXPECT_EQ(clone->GetUnmappedWireValue(MediaPacket::WireField::Flag), 10);
	EXPECT_FALSE(clone->GetUnmappedWireValue(MediaPacket::WireField::PacketType).has_value());
}

// A packet with all four header enums unmapped goes out with all four original bytes
TEST(OvtWireTest, AllFourUnmappedBytesRelay)
{
	const std::vector<uint8_t> data = {0x01};
	auto received					= Depacketize(MakeMediaPayload(3, 0x09, 0x04, 0xEE, 0x77, data));
	ASSERT_TRUE(received != nullptr);

	auto header = PacketizeHeader(received);
	ASSERT_EQ(header.size(), static_cast<size_t>(MEDIA_PACKET_HEADER_SIZE));
	EXPECT_EQ(header[28], 0x09);
	EXPECT_EQ(header[29], 0x04);
	EXPECT_EQ(header[30], 0xEE);
	EXPECT_EQ(header[31], 0x77);
	EXPECT_EQ(header[3], 3);
}

// Known bytes never leave a preserved slot behind
TEST(OvtWireTest, KnownBytesLeaveNoUnmappedSlot)
{
	const std::vector<uint8_t> data = {0x01};
	auto received					= Depacketize(MakeMediaPayload(1, 0x01, 0x01, 0x0A, 0x01, data));
	ASSERT_TRUE(received != nullptr);
	for (auto field : {MediaPacket::WireField::MediaType, MediaPacket::WireField::Flag, MediaPacket::WireField::BitstreamFormat, MediaPacket::WireField::PacketType})
	{
		EXPECT_FALSE(received->GetUnmappedWireValue(field).has_value());
	}
}

// A channel layout value is the OR of its channel bits, so the list is a membership test
TEST(OvtWireTest, EveryDefinedChannelLayoutIsFound)
{
	for (auto layout : cmn::AudioChannel::ALL_AUDIO_CHANNEL_LAYOUTS)
	{
		EXPECT_EQ(cmn::AudioChannel::GetLayoutByValue(static_cast<uint32_t>(layout)), layout);
	}

	// `LayoutUnknown` is the absence of a layout, so it is not in the list
	EXPECT_FALSE(cmn::AudioChannel::GetLayoutByValue(
					 static_cast<uint32_t>(cmn::AudioChannel::Layout::LayoutUnknown))
					 .has_value());

	// A combination this build does not define
	EXPECT_FALSE(cmn::AudioChannel::GetLayoutByValue(0xDEADBEEF).has_value());
	EXPECT_FALSE(cmn::AudioChannel::GetLayoutByValue(0xFFFFFFFF).has_value());
}

// A layout without an entry is kept as received so a relay hands the next hop the original
TEST(OvtWireTest, AnUndefinedChannelLayoutIsKept)
{
	MediaTrack track;

	track.SetChannelLayout(cmn::AudioChannel::Layout::LayoutUnknown);
	track.SetUnmappedChannelLayout(0xDEADBEEF);
	EXPECT_EQ(track.GetChannel().GetLayout(), cmn::AudioChannel::Layout::LayoutUnknown);
	EXPECT_EQ(track.GetChannel().GetUnmappedLayout(), 0xDEADBEEFu);

	// The audio parsers downstream only tell mono from stereo, so what they set must not take
	// the original away: the next hop may be a build that knows the layout
	track.SetChannelLayout(cmn::AudioChannel::Layout::LayoutStereo);
	EXPECT_EQ(track.GetChannel().GetUnmappedLayout(), 0xDEADBEEFu);

	// A copy carries it, which is what a bypassed track is
	MediaTrack copy(track);
	EXPECT_EQ(copy.GetChannel().GetUnmappedLayout(), 0xDEADBEEFu);

	// A layout this build defines keeps nothing
	MediaTrack known;
	known.SetChannelLayout(cmn::AudioChannel::Layout::Layout5Point1);
	known.SetUnmappedChannelLayout(std::nullopt);
	EXPECT_FALSE(known.GetChannel().GetUnmappedLayout().has_value());
}
