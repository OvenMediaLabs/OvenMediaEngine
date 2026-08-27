//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "rtmp_media_frame.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

//  Covers which FLV tag layouts count as a coded frame, on both the legacy and the E-RTMP path.
namespace
{
	std::shared_ptr<ov::Data> Tag(std::initializer_list<uint8_t> bytes)
	{
		const std::vector<uint8_t> buffer(bytes);

		if (buffer.empty())
		{
			return std::make_shared<ov::Data>();
		}

		return std::make_shared<ov::Data>(buffer.data(), buffer.size());
	}

	// The E-RTMP overloads take a parsed message, so these fill only the fields it reads.
	std::shared_ptr<modules::flv::AudioData> ExAudio(modules::flv::AudioPacketType packet_type)
	{
		auto data				= std::make_shared<modules::flv::AudioData>(0, modules::flv::SoundFormat::ExHeader, true);
		data->audio_packet_type = packet_type;

		return data;
	}

	std::shared_ptr<modules::flv::AudioData> LegacyAacAudio(modules::flv::AACPacketType packet_type)
	{
		auto data			  = std::make_shared<modules::flv::AudioData>(0, modules::flv::SoundFormat::Aac, false);
		data->aac_packet_type = packet_type;

		return data;
	}

	std::shared_ptr<modules::flv::VideoData> ExVideo(modules::flv::VideoPacketType packet_type)
	{
		auto data				= std::make_shared<modules::flv::VideoData>(0, modules::flv::VideoFrameType::KeyFrame, true);
		data->video_packet_type = packet_type;

		return data;
	}

	// The legacy AVC path builds the same struct with `from_ex_header` false,
	// after reading the `AvcPacketType` byte into `video_packet_type`.
	std::shared_ptr<modules::flv::VideoData> LegacyAvcVideo(modules::flv::VideoPacketType packet_type)
	{
		auto data				= std::make_shared<modules::flv::VideoData>(0, modules::flv::VideoFrameType::KeyFrame, false);
		data->video_packet_type = packet_type;

		return data;
	}
}  // namespace

//--------------------------------------------------------------------
// The legacy path reads the tag header byte by byte
//--------------------------------------------------------------------

TEST(RtmpMediaFrame, AnAvcNaluIsAFrame)
{
	// frame type 1 (key), codec id 7 (AVC), AVCPacketType 1
	EXPECT_TRUE(pvd::rtmp::HasVideoFrame(Tag({0x17, 0x01, 0x00, 0x00, 0x00})));
	// frame type 2 (inter)
	EXPECT_TRUE(pvd::rtmp::HasVideoFrame(Tag({0x27, 0x01, 0x00, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, AnAvcSequenceHeaderIsNotAFrame)
{
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({0x17, 0x00, 0x00, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, AnAvcEndOfSequenceIsNotAFrame)
{
	// `EndOfSequence` has an empty body, and `ReceiveVideoMessage()` discards it.
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({0x17, 0x02, 0x00, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, AVideoInfoCommandFrameIsNotAFrame)
{
	// A command sits where the coded frame would be.
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({0x57, 0x01, 0x00, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, VideoThisPathCannotServiceIsNotAFrame)
{
	// codec id 2 (H.263)
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({0x12, 0x01, 0x00, 0x00, 0x00})));
	// codec id 4 (VP6)
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({0x14, 0x01, 0x00, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, AnEnhancedVideoTagHeaderIsNotAFrameOnTheLegacyPath)
{
	// The codec is a FOURCC this path cannot service, description or coded frame alike.

	// `SequenceStart` of `av01`
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({0x90, 0x61, 0x76, 0x30, 0x31})));
	// `CodedFrames` of `av01`
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({0x91, 0x61, 0x76, 0x30, 0x31})));
}

TEST(RtmpMediaFrame, RawAacIsAFrame)
{
	// sound format 10 (AAC), AACPacketType 1
	EXPECT_TRUE(pvd::rtmp::HasAudioFrame(Tag({0xAF, 0x01, 0x21, 0x10})));
}

TEST(RtmpMediaFrame, AnAacSequenceHeaderIsNotAFrame)
{
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(Tag({0xAF, 0x00, 0x12, 0x10})));
}

TEST(RtmpMediaFrame, AudioThisPathCannotServiceIsNotAFrame)
{
	// sound format 2 (MP3), first data byte is the 0xFF sync word
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(Tag({0x2F, 0xFF, 0xFB, 0x90})));
	// sound format 7 (G.711 A-law)
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(Tag({0x7F, 0x01, 0x00, 0x00})));
	// sound format 0 (linear PCM)
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(Tag({0x0F, 0x01, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, AnEnhancedAudioTagHeaderIsNotAFrameOnTheLegacyPath)
{
	// Sound format 9 keeps its `AudioPacketType` in the low nibble and its FOURCC in byte 1.

	// `SequenceStart` of `Opus`
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(Tag({0x90, 0x4F, 0x70, 0x75, 0x73})));
	// `CodedFrames` of `Opus`
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(Tag({0x91, 0x4F, 0x70, 0x75, 0x73})));
}

TEST(RtmpMediaFrame, AMessageTooShortToHaveATagHeaderIsNotAFrame)
{
	// Pins the bounds guard; the callers reject a message this short before asking.
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({})));
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(Tag({0x17})));
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(nullptr));
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(Tag({})));
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(Tag({0xAF})));
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(nullptr));
}

//--------------------------------------------------------------------
// The E-RTMP path has its message parsed already
//--------------------------------------------------------------------

TEST(RtmpMediaFrame, ExAudioCodedFramesIsAFrame)
{
	EXPECT_TRUE(pvd::rtmp::HasAudioFrame(*ExAudio(modules::flv::AudioPacketType::CodedFrames)));
}

TEST(RtmpMediaFrame, EveryOtherExAudioPacketTypeIsNotAFrame)
{
	// `MultichannelConfig` maps channels and `SequenceEnd` means silence.
	// `ModEx` and `Multitrack` are overwritten by the parser before a message reaches here.
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(*ExAudio(modules::flv::AudioPacketType::SequenceStart)));
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(*ExAudio(modules::flv::AudioPacketType::SequenceEnd)));
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(*ExAudio(modules::flv::AudioPacketType::MultichannelConfig)));
	// reserved
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(*ExAudio(static_cast<modules::flv::AudioPacketType>(3))));
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(*ExAudio(static_cast<modules::flv::AudioPacketType>(6))));
}

TEST(RtmpMediaFrame, AParsedLegacyAacMessageFollowsItsOwnPacketType)
{
	EXPECT_TRUE(pvd::rtmp::HasAudioFrame(*LegacyAacAudio(modules::flv::AACPacketType::Raw)));
	EXPECT_FALSE(pvd::rtmp::HasAudioFrame(*LegacyAacAudio(modules::flv::AACPacketType::SequenceHeader)));
}

TEST(RtmpMediaFrame, ExVideoCodedFramesIsAFrame)
{
	EXPECT_TRUE(pvd::rtmp::HasVideoFrame(*ExVideo(modules::flv::VideoPacketType::CodedFrames)));
	// `CodedFramesX` leaves the composition time offset off the wire.
	EXPECT_TRUE(pvd::rtmp::HasVideoFrame(*ExVideo(modules::flv::VideoPacketType::CodedFramesX)));
}

TEST(RtmpMediaFrame, EveryOtherExVideoPacketTypeIsNotAFrame)
{
	// `MPEG2TSSequenceStart` describes an MPEG-2 TS bitstream.
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(*ExVideo(modules::flv::VideoPacketType::SequenceStart)));
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(*ExVideo(modules::flv::VideoPacketType::SequenceEnd)));
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(*ExVideo(modules::flv::VideoPacketType::Metadata)));
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(*ExVideo(modules::flv::VideoPacketType::MPEG2TSSequenceStart)));
	// reserved
	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(*ExVideo(static_cast<modules::flv::VideoPacketType>(9))));
}

TEST(RtmpMediaFrame, AParsedVideoCommandFrameIsNotAFrame)
{
	auto data				= std::make_shared<modules::flv::VideoData>(0, modules::flv::VideoFrameType::Command, true);
	data->video_packet_type = modules::flv::VideoPacketType::CodedFrames;

	EXPECT_FALSE(pvd::rtmp::HasVideoFrame(*data));
}

//--------------------------------------------------------------------
// The two paths must not disagree about the same bytes
//--------------------------------------------------------------------

TEST(RtmpMediaFrame, BothPathsAgreeOnLegacyAvc)
{
	struct
	{
		uint8_t packet_type;
		bool is_frame;
	} cases[] = {
		{0x00, false},	// `SequenceHeader`
		{0x01, true},	// NALU
		{0x02, false},	// `EndOfSequence`
	};

	for (const auto &c : cases)
	{
		EXPECT_EQ(pvd::rtmp::HasVideoFrame(Tag({0x17, c.packet_type, 0x00, 0x00, 0x00})), c.is_frame)
			<< "legacy path, packet type " << static_cast<int>(c.packet_type);

		// The E-RTMP parser reads the same byte into `video_packet_type`.
		EXPECT_EQ(pvd::rtmp::HasVideoFrame(*LegacyAvcVideo(static_cast<modules::flv::VideoPacketType>(c.packet_type))), c.is_frame)
			<< "E-RTMP path, packet type " << static_cast<int>(c.packet_type);
	}
}

TEST(RtmpMediaFrame, BothPathsAgreeOnLegacyAac)
{
	EXPECT_EQ(pvd::rtmp::HasAudioFrame(Tag({0xAF, 0x00, 0x12, 0x10})),
			  pvd::rtmp::HasAudioFrame(*LegacyAacAudio(modules::flv::AACPacketType::SequenceHeader)));
	EXPECT_EQ(pvd::rtmp::HasAudioFrame(Tag({0xAF, 0x01, 0x21, 0x10})),
			  pvd::rtmp::HasAudioFrame(*LegacyAacAudio(modules::flv::AACPacketType::Raw)));
}
