//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Jeheon Han
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/info/media_track.h>
#include <gtest/gtest.h>

TEST(MediaTrackUpdate, CopiesColorFields)
{
	MediaTrack source;
	source.SetId(1);
	source.SetMediaType(cmn::MediaType::Video);
	source.SetColorMatrix(cmn::ColorMatrix::BT709);
	source.SetColorRange(cmn::ColorRange::Limited);

	MediaTrack updated;
	updated.SetId(1);
	ASSERT_TRUE(updated.Update(source));
	EXPECT_EQ(updated.GetColorMatrix(), cmn::ColorMatrix::BT709);
	EXPECT_EQ(updated.GetColorRange(), cmn::ColorRange::Limited);

	MediaTrack copied(source);
	EXPECT_EQ(copied.GetColorMatrix(), cmn::ColorMatrix::BT709);
	EXPECT_EQ(copied.GetColorRange(), cmn::ColorRange::Limited);
}
