//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "../../providers/rtmp/tracks/rtmp_av1_track.h"

#include <base/info/media_track.h>
#include <gtest/gtest.h>

#include "../../providers/rtmp/tracks/rtmp_track.h"

TEST(RtmpAv1Track, CreateReturnsNonNullForAv1)
{
	auto track = pvd::rtmp::RtmpTrack::Create(nullptr, 0, true, cmn::MediaCodecId::Av1);

	ASSERT_NE(track, nullptr);
	EXPECT_EQ(track->GetCodecId(), cmn::MediaCodecId::Av1);
	EXPECT_EQ(track->GetBitstreamFormat(), cmn::BitstreamFormat::AV1_OBU);
	EXPECT_EQ(track->GetMediaType(), cmn::MediaType::Video);
}

TEST(RtmpAv1Track, FillMediaTrackMetadataAppliesCommonFields)
{
	auto track = pvd::rtmp::RtmpTrack::Create(nullptr, 7, true, cmn::MediaCodecId::Av1);
	ASSERT_NE(track, nullptr);

	auto media_track = std::make_shared<MediaTrack>();
	track->FillMediaTrackMetadata(media_track);

	EXPECT_EQ(media_track->GetId(), 7U);
	EXPECT_EQ(media_track->GetMediaType(), cmn::MediaType::Video);
	EXPECT_EQ(media_track->GetCodecId(), cmn::MediaCodecId::Av1);
	EXPECT_EQ(media_track->GetOriginBitstream(), cmn::BitstreamFormat::AV1_OBU);
}
