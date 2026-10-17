//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: the codec parameter fields (codecs, profile, level, tier, bitDepth)
//  of a video track in serdes::JsonFromTrack()
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/bitstream/av1/av1_decoder_configuration_record.h>
#include <modules/json_serdes/stream.h>

#include <vector>

namespace
{
	const char *kCodecParameterKeys[] = {"codecs", "profile", "level", "tier", "bitDepth"};

	std::shared_ptr<MediaTrack> MakeAv1Track()
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetMediaType(cmn::MediaType::Video);
		track->SetCodecId(cmn::MediaCodecId::Av1);
		track->SetTimeBase(1, 90000);
		return track;
	}

	// av1C of seq_profile 0, seq_level_idx 8 (4.0), Main tier, 10 bit 4:2:0
	std::shared_ptr<AV1DecoderConfigurationRecord> MakeAv1Config()
	{
		const std::vector<uint8_t> av1c = {0x81, 0x08, 0x4C, 0x00};

		auto config = std::make_shared<AV1DecoderConfigurationRecord>();
		if (config->Parse(std::make_shared<ov::Data>(av1c.data(), av1c.size())) == false)
		{
			return nullptr;
		}

		return config;
	}
}  // namespace

// An empty enhanced RTMP SequenceStart leaves the default av1C on the track until the in-band
// sequence header arrives, so nothing may be reported before the track is ready
TEST(JsonSerdesTrack, ReportsTheCodecParametersOnlyOnceTheTrackIsReady)
{
	auto track = MakeAv1Track();
	track->SetDecoderConfigurationRecord(std::make_shared<AV1DecoderConfigurationRecord>());
	ASSERT_FALSE(track->IsValid());

	auto video = serdes::JsonFromTrack(track)["video"];
	for (auto key : kCodecParameterKeys)
	{
		EXPECT_FALSE(video.isMember(key)) << key;
	}

	auto config = MakeAv1Config();
	ASSERT_NE(config, nullptr);
	track->SetDecoderConfigurationRecord(config);
	track->SetResolution(640, 360);
	ASSERT_TRUE(track->IsValid());

	video = serdes::JsonFromTrack(track)["video"];
	EXPECT_EQ(video["codecs"].asString(), "av01.0.08M.10");
	EXPECT_EQ(video["profile"].asString(), "Main");
	EXPECT_EQ(video["level"].asString(), "4.0");
	EXPECT_EQ(video["tier"].asString(), "Main");
	EXPECT_EQ(video["bitDepth"].asInt(), 10);
}
