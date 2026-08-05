//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/info/stream.h>

#include "transcoder_encoder.h"

// KeyframeGridRestore: after a pipeline disturbance (track change, codec
// session reinitialization) the encoder forces one keyframe where the previous
// cadence would have produced one, so the cadence does not shift permanently.
// FRAME mode mirrors the codec's own frame counting; TIME mode projects the
// next position from the last output keyframe timestamp.

namespace
{
	class GridRestoreTestEncoder : public TranscodeEncoder
	{
	public:
		GridRestoreTestEncoder(const std::shared_ptr<MediaTrack> &track)
			: TranscodeEncoder(info::Stream(StreamSourceType::Transcoder))
		{
			_track = track;
		}

		cmn::AudioSample::Format GetSupportAudioFormat() const noexcept override
		{
			return cmn::AudioSample::Format::None;
		}
		cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept override
		{
			return cmn::VideoPixelFormatId::YUV420P;
		}
		cmn::BitstreamFormat GetBitstreamFormat() const noexcept override
		{
			return cmn::BitstreamFormat::H264_ANNEXB;
		}
		cmn::MediaCodecId GetCodecID() const noexcept override
		{
			return cmn::MediaCodecId::H264;
		}
		cmn::MediaCodecModuleId GetModuleID() const noexcept override
		{
			return cmn::MediaCodecModuleId::DEFAULT;
		}
		cmn::MediaType GetMediaType() const noexcept override
		{
			return cmn::MediaType::Video;
		}
		bool IsHWAccel() const noexcept override
		{
			return false;
		}
		bool Initialize() override
		{
			return true;
		}

		// One frame through the per-frame cadence check, as ThreadLoop does
		bool Step(int64_t frame_pts)
		{
			auto frame = std::make_shared<MediaFrame>();
			frame->SetPts(frame_pts);
			return ComputeKeyframeGridRestore(frame);
		}

		// The last output keyframe position is normally tracked in Complete();
		// seed it the same way with a fake key packet
		void SeedLastKeyframe(int64_t pts)
		{
			auto data = std::make_shared<ov::Data>();
			uint8_t byte = 0x00;
			data->Append(&byte, 1);
			auto packet = std::make_shared<MediaPacket>(cmn::MediaType::Video, 0, data, pts, pts, 0,
														MediaPacketFlag::Key, cmn::BitstreamFormat::H264_ANNEXB, cmn::PacketType::NALU);
			Complete(TranscodeResult::DataReady, std::move(packet));
		}

		bool IsArmed() const
		{
			return _keyframe_grid_restore_armed;
		}
	};

	constexpr int32_t kIntervalFrames = 30;
	constexpr int64_t kFrameTicks = 3000;  // 1/90000 timebase, 30 fps

	std::shared_ptr<MediaTrack> MakeVideoTrack(cmn::KeyFrameIntervalType type = cmn::KeyFrameIntervalType::FRAME)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(0);
		track->SetMediaType(cmn::MediaType::Video);
		track->SetCodecId(cmn::MediaCodecId::H264);
		track->SetTimeBase(1, 90000);
		track->SetFrameRateByConfig(30.0);
		track->SetKeyFrameIntervalTypeByConfig(type);
		// FRAME: 30 frames, TIME: 1000 ms; both are one second at 30 fps
		track->SetKeyFrameIntervalByConfig(type == cmn::KeyFrameIntervalType::FRAME ? kIntervalFrames : 1000);
		return track;
	}
}  // namespace

TEST(KeyframeGridRestoreTest, FrameModeFiresAtNextCadencePosition)
{
	GridRestoreTestEncoder encoder(MakeVideoTrack());

	// Build up mid-cycle cadence state: 10 frames into the cycle
	int64_t pts = 0;
	for (int step = 0; step < 10; step++)
	{
		EXPECT_FALSE(encoder.Step(pts));
		pts += kFrameTicks;
	}

	encoder.ArmKeyframeGridRestore();

	// The cadence position is the 31st frame of the cycle: 20 more pass first
	for (int step = 0; step < 20; step++)
	{
		EXPECT_FALSE(encoder.Step(pts)) << "fired early at step " << step;
		pts += kFrameTicks;
	}

	EXPECT_TRUE(encoder.Step(pts));
	EXPECT_FALSE(encoder.IsArmed());
}

TEST(KeyframeGridRestoreTest, FrameModeFiresOnlyWhileArmed)
{
	GridRestoreTestEncoder encoder(MakeVideoTrack());

	// Two full cadence cycles without arming: the counter runs, nothing fires
	int64_t pts = 0;
	for (int step = 0; step < kIntervalFrames * 2 + 1; step++)
	{
		EXPECT_FALSE(encoder.Step(pts)) << "fired unarmed at step " << step;
		pts += kFrameTicks;
	}

	// Armed now: the next cadence position fires exactly once
	encoder.ArmKeyframeGridRestore();
	bool fired = false;
	for (int step = 0; step <= kIntervalFrames; step++)
	{
		if (encoder.Step(pts))
		{
			fired = true;
			break;
		}
		pts += kFrameTicks;
	}
	EXPECT_TRUE(fired);
	EXPECT_FALSE(encoder.IsArmed());
}

TEST(KeyframeGridRestoreTest, FrameModeDisarmsWithoutInterval)
{
	auto track = MakeVideoTrack();
	track->SetKeyFrameIntervalByConfig(0);	// encoder-decided cadence
	GridRestoreTestEncoder encoder(track);

	encoder.ArmKeyframeGridRestore();
	EXPECT_FALSE(encoder.Step(0));
	EXPECT_FALSE(encoder.IsArmed());
}

TEST(KeyframeGridRestoreTest, TimeModeFiresAtProjectedPosition)
{
	GridRestoreTestEncoder encoder(MakeVideoTrack(cmn::KeyFrameIntervalType::TIME));

	// The last keyframe went out at 10 s
	encoder.SeedLastKeyframe(10 * 90000);
	encoder.ArmKeyframeGridRestore();

	EXPECT_FALSE(encoder.Step(10 * 90000 + kFrameTicks));
	EXPECT_FALSE(encoder.Step(11 * 90000 - kFrameTicks));
	EXPECT_TRUE(encoder.Step(11 * 90000));
	EXPECT_FALSE(encoder.IsArmed());
	EXPECT_FALSE(encoder.Step(12 * 90000));
}

TEST(KeyframeGridRestoreTest, TimeModeDisarmsWithoutCadenceHistory)
{
	GridRestoreTestEncoder encoder(MakeVideoTrack(cmn::KeyFrameIntervalType::TIME));

	// Armed but no keyframe has ever been produced: nothing to restore
	encoder.ArmKeyframeGridRestore();
	EXPECT_FALSE(encoder.Step(90000));
	EXPECT_FALSE(encoder.IsArmed());
}
