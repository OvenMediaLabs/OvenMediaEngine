//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Jeheon Han
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/ffmpeg/compat.h>

// cmn::ColorMatrix mirrors the H.273 values of AVColorSpace, and the compat
// converters rely on that 1:1 mapping. These tests pin the invariant.

TEST(FFmpegCompatColor, ColorMatrixMirrorsAVColorSpace)
{
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::RGB), static_cast<int>(AVCOL_SPC_RGB));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::BT709), static_cast<int>(AVCOL_SPC_BT709));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::Unspecified), static_cast<int>(AVCOL_SPC_UNSPECIFIED));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::FCC), static_cast<int>(AVCOL_SPC_FCC));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::BT470BG), static_cast<int>(AVCOL_SPC_BT470BG));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::SMPTE170M), static_cast<int>(AVCOL_SPC_SMPTE170M));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::SMPTE240M), static_cast<int>(AVCOL_SPC_SMPTE240M));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::YCGCO), static_cast<int>(AVCOL_SPC_YCGCO));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::BT2020NCL), static_cast<int>(AVCOL_SPC_BT2020_NCL));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::BT2020CL), static_cast<int>(AVCOL_SPC_BT2020_CL));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::SMPTE2085), static_cast<int>(AVCOL_SPC_SMPTE2085));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::ChromaDerivedNCL), static_cast<int>(AVCOL_SPC_CHROMA_DERIVED_NCL));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::ChromaDerivedCL), static_cast<int>(AVCOL_SPC_CHROMA_DERIVED_CL));
	EXPECT_EQ(static_cast<int>(cmn::ColorMatrix::ICTCP), static_cast<int>(AVCOL_SPC_ICTCP));
}

TEST(FFmpegCompatColor, ColorSpaceRoundTrip)
{
	// Every valid AVColorSpace value survives the round trip, including values
	// that have no named cmn::ColorMatrix enumerator (e.g. RESERVED)
	for (int value = 0; value < AVCOL_SPC_NB; value++)
	{
		auto av_color_space = static_cast<enum AVColorSpace>(value);
		EXPECT_EQ(ffmpeg::compat::ToAVColorSpace(ffmpeg::compat::ToColorMatrix(av_color_space)), av_color_space);
	}
}

TEST(FFmpegCompatColor, ColorSpaceOutOfRangeFallsBackToUnspecified)
{
	EXPECT_EQ(ffmpeg::compat::ToColorMatrix(static_cast<enum AVColorSpace>(-1)), cmn::ColorMatrix::Unspecified);
	EXPECT_EQ(ffmpeg::compat::ToColorMatrix(AVCOL_SPC_NB), cmn::ColorMatrix::Unspecified);
	EXPECT_EQ(ffmpeg::compat::ToAVColorSpace(static_cast<cmn::ColorMatrix>(-1)), AVCOL_SPC_UNSPECIFIED);
}

TEST(FFmpegCompatColor, ColorRangeRoundTrip)
{
	EXPECT_EQ(ffmpeg::compat::ToColorRange(AVCOL_RANGE_UNSPECIFIED), cmn::ColorRange::Unspecified);
	EXPECT_EQ(ffmpeg::compat::ToColorRange(AVCOL_RANGE_MPEG), cmn::ColorRange::Limited);
	EXPECT_EQ(ffmpeg::compat::ToColorRange(AVCOL_RANGE_JPEG), cmn::ColorRange::Full);

	EXPECT_EQ(ffmpeg::compat::ToAVColorRange(cmn::ColorRange::Unspecified), AVCOL_RANGE_UNSPECIFIED);
	EXPECT_EQ(ffmpeg::compat::ToAVColorRange(cmn::ColorRange::Limited), AVCOL_RANGE_MPEG);
	EXPECT_EQ(ffmpeg::compat::ToAVColorRange(cmn::ColorRange::Full), AVCOL_RANGE_JPEG);
}
