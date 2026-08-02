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

//  Covers which FLV tag layouts count as an input's first frame.
//  Ending the wait on a codec description makes `FirstMediaWaitTimeoutMs` do nothing.
//  Never ending it leaves an unpublished channel on the operator's larger timeout instead.
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
}  // namespace

TEST(RtmpMediaFrame, AvcSequenceHeaderIsNotAFrame)
{
	// frame type 1 (key), codec id 7 (AVC), AVCPacketType 0
	EXPECT_FALSE(pvd::rtmp::CarriesVideoFrame(Tag({0x17, 0x00, 0x00, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, AvcNaluIsAFrame)
{
	EXPECT_TRUE(pvd::rtmp::CarriesVideoFrame(Tag({0x17, 0x01, 0x00, 0x00, 0x00})));
	// frame type 2 (inter)
	EXPECT_TRUE(pvd::rtmp::CarriesVideoFrame(Tag({0x27, 0x01, 0x00, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, VideoThisProviderCannotServiceIsAFrame)
{
	// `flv::VideoData::Parse()` rejects a codec id other than AVC and logs an error for it.
	// Deciding this through the parser would answer wrongly and log once per pre-publish message.

	// codec id 2 (H.263)
	EXPECT_TRUE(pvd::rtmp::CarriesVideoFrame(Tag({0x12, 0x00, 0x00, 0x00, 0x00})));
	// codec id 4 (VP6)
	EXPECT_TRUE(pvd::rtmp::CarriesVideoFrame(Tag({0x14, 0x00, 0x00, 0x00, 0x00})));
	// enhanced video tag header: the low nibble is `videoPacketType`, not a codec id
	EXPECT_TRUE(pvd::rtmp::CarriesVideoFrame(Tag({0x90, 0x68, 0x76, 0x63, 0x31})));
}

TEST(RtmpMediaFrame, AacSequenceHeaderIsNotAFrame)
{
	// sound format 10 (AAC), AACPacketType 0
	EXPECT_FALSE(pvd::rtmp::CarriesAudioFrame(Tag({0xAF, 0x00, 0x12, 0x10})));
}

TEST(RtmpMediaFrame, AacRawIsAFrame)
{
	EXPECT_TRUE(pvd::rtmp::CarriesAudioFrame(Tag({0xAF, 0x01, 0x21, 0x10})));
}

TEST(RtmpMediaFrame, AudioWithoutAPacketTypeByteIsAFrame)
{
	// Only AAC carries the packet type byte.
	// For every other sound format that byte is media data, so reading it as a packet type decides this
	// at random: an MP3 sync word answers one way and a PCM sample answers another.
	// sound format 2 (MP3), first data byte is the 0xFF sync word
	EXPECT_TRUE(pvd::rtmp::CarriesAudioFrame(Tag({0x2F, 0xFF, 0xFB, 0x90})));
	// sound format 7 (G.711 A-law), a sample that happens to be zero
	EXPECT_TRUE(pvd::rtmp::CarriesAudioFrame(Tag({0x7F, 0x00, 0x00, 0x00})));
	// sound format 0 (linear PCM), a sample that happens to be one
	EXPECT_TRUE(pvd::rtmp::CarriesAudioFrame(Tag({0x0F, 0x01, 0x00, 0x00})));
}

TEST(RtmpMediaFrame, AMessageTooShortToHoldADescriptionIsAFrame)
{
	// An audio message with no tag header is RTMP's silence message. Neither this nor a truncated video
	// header describes a codec, so neither may hold the wait open.
	EXPECT_TRUE(pvd::rtmp::CarriesAudioFrame(Tag({})));
	EXPECT_TRUE(pvd::rtmp::CarriesAudioFrame(Tag({0xAF})));
	EXPECT_TRUE(pvd::rtmp::CarriesVideoFrame(Tag({0x17})));
	EXPECT_TRUE(pvd::rtmp::CarriesVideoFrame(nullptr));
	EXPECT_TRUE(pvd::rtmp::CarriesAudioFrame(nullptr));
}
