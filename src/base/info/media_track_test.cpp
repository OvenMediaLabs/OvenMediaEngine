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

// A layout this build has no name for reads as `LayoutUnknown` with the raw value kept beside it,
// so content equality has to look at the raw value or two different layouts compare equal and the
// change is never relayed.
TEST(MediaTrackContent, UnknownLayoutsAreToldApartByTheRawValue)
{
	auto make = [](std::optional<uint32_t> wire, cmn::AudioChannel::Layout layout) {
		MediaTrack track;
		track.SetId(1);
		track.SetMediaType(cmn::MediaType::Audio);
		track.SetCodecId(cmn::MediaCodecId::Aac);
		track.SetTimeBase(1, 48000);
		track.SetSampleRate(48000);
		track.SetSampleFormat(cmn::AudioSample::Format::S16);
		track.SetChannelLayout(layout);
		track.SetUnmappedChannelLayout(wire);
		return track;
	};

	auto unknown_a = make(0x1111u, cmn::AudioChannel::Layout::LayoutUnknown);
	auto unknown_b = make(0x2222u, cmn::AudioChannel::Layout::LayoutUnknown);
	auto same_as_a = make(0x1111u, cmn::AudioChannel::Layout::LayoutUnknown);
	auto known	   = make(std::nullopt, cmn::AudioChannel::Layout::LayoutStereo);

	EXPECT_EQ(unknown_a.GetChannel().GetLayout(), unknown_b.GetChannel().GetLayout());
	EXPECT_FALSE(unknown_a.HasSameContent(unknown_b));
	EXPECT_TRUE(unknown_a.HasSameContent(same_as_a));
	EXPECT_FALSE(unknown_a.HasSameContent(known));
}
