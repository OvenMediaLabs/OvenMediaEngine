#include <gtest/gtest.h>

#include <base/mediarouter/media_type.h>
#include <modules/containers/mpegts/mpegts_section.h>
#include <modules/ffmpeg/compat.h>

TEST(MpegTsAudioSupport, RecognizesMpegAudioStreamTypes)
{
	EXPECT_EQ(static_cast<uint8_t>(mpegts::WellKnownStreamTypes::MPEG1_AUDIO), 0x03);
	EXPECT_EQ(static_cast<uint8_t>(mpegts::WellKnownStreamTypes::MPEG2_AUDIO), 0x04);
}

TEST(MpegTsAudioSupport, MapsMp2CodecAndBitstream)
{
	EXPECT_TRUE(cmn::IsAudioCodec(cmn::MediaCodecId::Mp2));
	EXPECT_STREQ(cmn::GetCodecIdString(cmn::MediaCodecId::Mp2), "MP2");
	EXPECT_STREQ(cmn::GetBitstreamFormatString(cmn::BitstreamFormat::MP2), "MP2");
	EXPECT_EQ(cmn::GetCodecIdByName("mp2"), cmn::MediaCodecId::Mp2);
}

TEST(MpegTsAudioSupport, MapsMp2ToFfmpegCodecId)
{
	EXPECT_EQ(ffmpeg::compat::ToCodecId(AV_CODEC_ID_MP2), cmn::MediaCodecId::Mp2);
	EXPECT_EQ(ffmpeg::compat::ToAVCodecId(cmn::MediaCodecId::Mp2), AV_CODEC_ID_MP2);
}
