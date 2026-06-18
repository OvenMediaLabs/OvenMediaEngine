//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/ovlibrary/bit_reader_v2.h>
#include <base/ovlibrary/data.h>
#include <gtest/gtest.h>
#include <modules/bitstream/av1/av1_decoder_configuration_record.h>
#include <modules/containers/flv_v2/flv_video_parser.h>

#include "../../providers/rtmp/tracks/rtmp_track.h"

#include <cstring>
#include <vector>

namespace
{
	constexpr uint32_t TRACK_ID		 = 100;

	// Enhanced RTMP ExVideoTag byte layout:
	//   bit 7     : isExVideoHeader (1 = enhanced)
	//   bits 6-4  : VideoFrameType  (1 = Key, 2 = Inter)
	//   bits 3-0  : VideoPacketType (0 = SequenceStart, 1 = CodedFrames)
	//   bytes 1-4 : FourCC ("av01" for AV1)
	//   bytes 5+  : body

	// FourCC "av01" in big-endian
	constexpr uint8_t AV1_FOURCC[]	 = {0x61, 0x76, 0x30, 0x31};

	// Valid minimal av1C: marker=1 version=1, seq_profile=0, seq_level_idx_0=8,
	// 4:2:0 subsampling, no configOBUs
	constexpr uint8_t MINIMAL_AV1C[] = {
		0x81,  // marker=1, version=1
		0x08,  // seq_profile=0, seq_level_idx_0=8
		0x0C,  // seq_tier_0=0, high_bitdepth=0, twelve_bit=0, monochrome=0, subsampling_x=1, subsampling_y=1, csp=0
		0x00   // initial_presentation_delay_present=0, reserved=0
	};

	// Dummy OBU payload (just bytes that survive passthrough)
	constexpr uint8_t DUMMY_OBU_PAYLOAD[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

	std::vector<uint8_t> BuildExVideoTag(uint8_t frame_type, uint8_t packet_type,
										 const uint8_t *fourcc,
										 const uint8_t *body, size_t body_len)
	{
		uint8_t header_byte = (1 << 7) | ((frame_type & 0x07) << 4) | (packet_type & 0x0F);

		std::vector<uint8_t> result;
		result.push_back(header_byte);
		result.insert(result.end(), fourcc, fourcc + 4);
		if (body != nullptr && body_len > 0)
		{
			result.insert(result.end(), body, body + body_len);
		}
		return result;
	}

	// Parse a raw ExVideoTag and return the first VideoData
	std::shared_ptr<modules::flv::VideoData> ParseAv1ExVideoTag(const std::vector<uint8_t> &raw)
	{
		auto data = std::make_shared<ov::Data>(raw.data(), raw.size());
		ov::BitReader reader(data);

		modules::flv::VideoParser parser(TRACK_ID);
		if (parser.Parse(reader) == false)
		{
			return nullptr;
		}

		auto &list = parser.GetDataList();
		if (list.empty())
		{
			return nullptr;
		}

		return list[0];
	}
}  // namespace

// ---------------------------------------------------------------------------
// SequenceStart with valid av1C
// ---------------------------------------------------------------------------
TEST(FlvAv1Parser, SequenceStartWithValidAv1C)
{
	auto raw		= BuildExVideoTag(1, 0, AV1_FOURCC, MINIMAL_AV1C, sizeof(MINIMAL_AV1C));

	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	EXPECT_TRUE(video_data->from_ex_header);
	EXPECT_EQ(video_data->video_packet_type, modules::flv::VideoPacketType::SequenceStart);
	ASSERT_TRUE(video_data->video_fourcc.has_value());
	EXPECT_EQ(video_data->video_fourcc.value(), modules::flv::VideoFourCc::Av1);
	EXPECT_TRUE(video_data->IsKeyFrame());

	// header must be a valid AV1DecoderConfigurationRecord
	ASSERT_NE(video_data->header, nullptr);
	auto av1_dcr = std::dynamic_pointer_cast<AV1DecoderConfigurationRecord>(video_data->header);
	ASSERT_NE(av1_dcr, nullptr);
	EXPECT_EQ(av1_dcr->SeqProfile(), 0);
	EXPECT_EQ(av1_dcr->SeqLevelIdx0(), 8);
	EXPECT_EQ(av1_dcr->ChromaSubsamplingX(), 1);
	EXPECT_EQ(av1_dcr->ChromaSubsamplingY(), 1);

	// header_data must contain the raw av1C bytes
	ASSERT_NE(video_data->header_data, nullptr);
	EXPECT_EQ(video_data->header_data->GetLength(), sizeof(MINIMAL_AV1C));
	EXPECT_EQ(std::memcmp(video_data->header_data->GetData(), MINIMAL_AV1C, sizeof(MINIMAL_AV1C)), 0);
}

// ---------------------------------------------------------------------------
// SequenceStart with empty body (ffmpeg libaom-av1 behavior)
// ---------------------------------------------------------------------------
TEST(FlvAv1Parser, SequenceStartWithEmptyBodySynthesizesDefault)
{
	// SequenceStart with no av1C body
	auto raw		= BuildExVideoTag(1, 0, AV1_FOURCC, nullptr, 0);

	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	EXPECT_EQ(video_data->video_packet_type, modules::flv::VideoPacketType::SequenceStart);
	ASSERT_TRUE(video_data->video_fourcc.has_value());
	EXPECT_EQ(video_data->video_fourcc.value(), modules::flv::VideoFourCc::Av1);

	// A synthesized default AV1DecoderConfigurationRecord must be present
	ASSERT_NE(video_data->header, nullptr);
	auto av1_dcr = std::dynamic_pointer_cast<AV1DecoderConfigurationRecord>(video_data->header);
	ASSERT_NE(av1_dcr, nullptr);

	// Synthesized defaults: marker=1, version=1, everything else zeroed
	EXPECT_EQ(av1_dcr->SeqProfile(), 0);
	EXPECT_EQ(av1_dcr->SeqLevelIdx0(), 0);

	// header_data is empty because 0 bytes were consumed from the reader.
	// The SEQUENCE_HEADER MediaPacket will carry empty data - this is by design;
	// MediaRouter relies on the in-band OBU_SEQUENCE_HEADER in the first CodedFrames.
	ASSERT_NE(video_data->header_data, nullptr);
	EXPECT_EQ(video_data->header_data->GetLength(), 0U);
}

// ---------------------------------------------------------------------------
// CodedFrames (key frame) - payload passthrough, no compositionTimeOffset
// ---------------------------------------------------------------------------
TEST(FlvAv1Parser, CodedFramesKeyFramePayloadPassthrough)
{
	auto raw		= BuildExVideoTag(1, 1, AV1_FOURCC, DUMMY_OBU_PAYLOAD, sizeof(DUMMY_OBU_PAYLOAD));

	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	EXPECT_EQ(video_data->video_packet_type, modules::flv::VideoPacketType::CodedFrames);
	ASSERT_TRUE(video_data->video_fourcc.has_value());
	EXPECT_EQ(video_data->video_fourcc.value(), modules::flv::VideoFourCc::Av1);
	EXPECT_TRUE(video_data->IsKeyFrame());

	// AV1 CodedFrames must NOT consume compositionTimeOffset from the wire
	EXPECT_EQ(video_data->composition_time_offset, 0);

	// payload must contain the entire OBU body
	ASSERT_NE(video_data->payload, nullptr);
	EXPECT_EQ(video_data->payload->GetLength(), sizeof(DUMMY_OBU_PAYLOAD));
	EXPECT_EQ(std::memcmp(video_data->payload->GetData(), DUMMY_OBU_PAYLOAD, sizeof(DUMMY_OBU_PAYLOAD)), 0);

	// No header for CodedFrames
	EXPECT_EQ(video_data->header, nullptr);
}

// ---------------------------------------------------------------------------
// CodedFrames (inter frame) - verify frame type propagates
// ---------------------------------------------------------------------------
TEST(FlvAv1Parser, CodedFramesInterFrame)
{
	auto raw		= BuildExVideoTag(2, 1, AV1_FOURCC, DUMMY_OBU_PAYLOAD, sizeof(DUMMY_OBU_PAYLOAD));

	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	EXPECT_EQ(video_data->video_packet_type, modules::flv::VideoPacketType::CodedFrames);
	EXPECT_FALSE(video_data->IsKeyFrame());
	EXPECT_EQ(video_data->composition_time_offset, 0);

	ASSERT_NE(video_data->payload, nullptr);
	EXPECT_EQ(video_data->payload->GetLength(), sizeof(DUMMY_OBU_PAYLOAD));
}

// ---------------------------------------------------------------------------
// Verify AV1 CodedFrames does NOT consume compositionTimeOffset,
// unlike AVC which would eat 3 bytes from the body as SI24 CTS.
// ---------------------------------------------------------------------------
TEST(FlvAv1Parser, CodedFramesNoCtsConsumed)
{
	// Build a payload where the first 3 bytes are non-zero.
	// If AV1 incorrectly consumed CTS like AVC does, these 3 bytes would be
	// eaten and the payload would be 3 bytes shorter.
	const uint8_t body[] = {0xFF, 0x01, 0x02, 0xAA, 0xBB};
	auto raw			 = BuildExVideoTag(1, 1, AV1_FOURCC, body, sizeof(body));

	auto video_data		 = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	// All 5 bytes must appear in the payload (no 3-byte CTS consumed)
	ASSERT_NE(video_data->payload, nullptr);
	EXPECT_EQ(video_data->payload->GetLength(), sizeof(body));
	EXPECT_EQ(std::memcmp(video_data->payload->GetData(), body, sizeof(body)), 0);
	EXPECT_EQ(video_data->composition_time_offset, 0);
}

// ---------------------------------------------------------------------------
// CodedFramesX for AV1 is not handled - should return nullptr
// ---------------------------------------------------------------------------
TEST(FlvAv1Parser, CodedFramesXReturnsNull)
{
	// VideoPacketType::CodedFramesX = 3
	auto raw		= BuildExVideoTag(1, 3, AV1_FOURCC, DUMMY_OBU_PAYLOAD, sizeof(DUMMY_OBU_PAYLOAD));

	auto video_data = ParseAv1ExVideoTag(raw);
	EXPECT_EQ(video_data, nullptr);
}

// ===========================================================================
// Pipeline: FLV parse -> VideoData -> verify packet-type mapping and data
// that RtmpVideoTrack::Handle() would produce.
//
// RtmpTrack::CreateMediaPacket() requires a non-null RtmpStreamV2 (for GetMsid()),
// so we verify the intermediate data structures that feed into it:
//   - VideoData fields (header, header_data, payload, composition_time_offset)
//   - ToCommonPacketType mapping
//   - Track sequence header state
// ===========================================================================

TEST(FlvAv1Pipeline, SequenceStartVideoPacketTypeIsCorrect)
{
	auto raw		= BuildExVideoTag(1, 0, AV1_FOURCC, MINIMAL_AV1C, sizeof(MINIMAL_AV1C));
	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	// RtmpVideoTrack::ToCommonPacketType maps SequenceStart -> SEQUENCE_HEADER
	EXPECT_EQ(video_data->video_packet_type, modules::flv::VideoPacketType::SequenceStart);
}

TEST(FlvAv1Pipeline, CodedFramesVideoPacketTypeIsCorrect)
{
	auto raw		= BuildExVideoTag(1, 1, AV1_FOURCC, DUMMY_OBU_PAYLOAD, sizeof(DUMMY_OBU_PAYLOAD));
	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	// RtmpVideoTrack::ToCommonPacketType maps CodedFrames -> NALU
	EXPECT_EQ(video_data->video_packet_type, modules::flv::VideoPacketType::CodedFrames);
}

TEST(FlvAv1Pipeline, SequenceStartHeaderDataMatchesAv1C)
{
	auto raw		= BuildExVideoTag(1, 0, AV1_FOURCC, MINIMAL_AV1C, sizeof(MINIMAL_AV1C));
	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	// Verify the data that would become the SEQUENCE_HEADER MediaPacket's payload
	ASSERT_NE(video_data->header_data, nullptr);
	EXPECT_EQ(video_data->header_data->GetLength(), sizeof(MINIMAL_AV1C));

	// Verify header is a valid AV1 DCR that MediaRouter can re-parse
	auto av1_dcr = std::dynamic_pointer_cast<AV1DecoderConfigurationRecord>(video_data->header);
	ASSERT_NE(av1_dcr, nullptr);

	// header_data must be parseable as av1C by MediaRouter
	auto reparsed = std::make_shared<AV1DecoderConfigurationRecord>();
	EXPECT_TRUE(reparsed->Parse(video_data->header_data));
	EXPECT_EQ(reparsed->SeqProfile(), av1_dcr->SeqProfile());
	EXPECT_EQ(reparsed->SeqLevelIdx0(), av1_dcr->SeqLevelIdx0());
}

TEST(FlvAv1Pipeline, CodedFramesPayloadIsObuData)
{
	auto raw		= BuildExVideoTag(1, 1, AV1_FOURCC, DUMMY_OBU_PAYLOAD, sizeof(DUMMY_OBU_PAYLOAD));
	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	// The payload is what becomes the NALU MediaPacket's data
	ASSERT_NE(video_data->payload, nullptr);
	EXPECT_EQ(video_data->payload->GetLength(), sizeof(DUMMY_OBU_PAYLOAD));
	EXPECT_EQ(std::memcmp(video_data->payload->GetData(), DUMMY_OBU_PAYLOAD, sizeof(DUMMY_OBU_PAYLOAD)), 0);

	// CTS must be 0 (not consumed from wire)
	EXPECT_EQ(video_data->composition_time_offset, 0);
}

TEST(FlvAv1Pipeline, EmptySequenceStartHeaderDataIsEmpty)
{
	auto raw		= BuildExVideoTag(1, 0, AV1_FOURCC, nullptr, 0);
	auto video_data = ParseAv1ExVideoTag(raw);
	ASSERT_NE(video_data, nullptr);

	// Synthesized DCR exists but header_data is 0 bytes.
	// MediaRouter receives empty SEQUENCE_HEADER packet data -> drops it ->
	// relies on in-band OBU_SEQUENCE_HEADER in first CodedFrames.
	ASSERT_NE(video_data->header, nullptr);
	ASSERT_NE(video_data->header_data, nullptr);
	EXPECT_EQ(video_data->header_data->GetLength(), 0U);
}

TEST(FlvAv1Pipeline, TrackBitstreamFormatIsAv1Obu)
{
	auto track = pvd::rtmp::RtmpTrack::Create(nullptr, TRACK_ID, true, cmn::MediaCodecId::Av1);
	ASSERT_NE(track, nullptr);

	// BitstreamFormat::AV1_OBU is what routes to ProcessAV1OBUStream in MediaRouter
	EXPECT_EQ(track->GetBitstreamFormat(), cmn::BitstreamFormat::AV1_OBU);
}
