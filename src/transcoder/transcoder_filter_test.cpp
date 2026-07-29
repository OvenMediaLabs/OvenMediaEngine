//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Jeheon Han
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <base/info/stream.h>
#include <modules/ffmpeg/compat.h>

#include "filter/filter_lavfi_resampler.h"
#include "transcoder_filter.h"

namespace
{
	std::shared_ptr<MediaTrack> MakeAudioTrack(int32_t id, int32_t sample_rate)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(id);
		track->SetMediaType(cmn::MediaType::Audio);
		track->SetCodecId(cmn::MediaCodecId::Aac);
		track->SetCodecModuleId(cmn::MediaCodecModuleId::DEFAULT);
		track->SetTimeBase(1, sample_rate);
		track->SetSampleRate(sample_rate);
		track->SetSampleFormat(cmn::AudioSample::Format::FltP);
		track->SetChannelLayout(cmn::AudioChannel::Layout::LayoutStereo);
		track->SetAudioSamplesPerFrame(1024);

		return track;
	}

	std::shared_ptr<MediaTrack> MakeVideoTrack(int32_t id, int32_t width, int32_t height)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(id);
		track->SetMediaType(cmn::MediaType::Video);
		track->SetCodecId(cmn::MediaCodecId::H264);
		track->SetCodecModuleId(cmn::MediaCodecModuleId::DEFAULT);
		track->SetTimeBase(1, 90000);
		track->SetFrameRateByConfig(30.0);
		track->SetResolution(width, height);
		track->SetColorspace(cmn::VideoPixelFormatId::YUV420P);

		return track;
	}

	std::shared_ptr<MediaFrame> MakeVideoFrame(int32_t width, int32_t height, enum AVPixelFormat pix_fmt,
											   enum AVColorSpace color_space, enum AVColorRange color_range, int64_t pts)
	{
		AVFrame *av_frame	  = ::av_frame_alloc();
		av_frame->format	  = pix_fmt;
		av_frame->width		  = width;
		av_frame->height	  = height;
		av_frame->colorspace  = color_space;
		av_frame->color_range = color_range;
		av_frame->pts		  = pts;

		if (::av_frame_get_buffer(av_frame, 0) < 0)
		{
			::av_frame_free(&av_frame);
			return nullptr;
		}

		auto media_frame = ffmpeg::compat::ToMediaFrame(cmn::MediaType::Video, av_frame);
		::av_frame_free(&av_frame);

		return media_frame;
	}

	// A silent stereo fltp frame, built through the same path as decoder output
	std::shared_ptr<MediaFrame> MakeAudioFrame(int32_t sample_rate, int64_t pts, int32_t nb_samples)
	{
		AVFrame *av_frame	 = ::av_frame_alloc();
		av_frame->format	 = AV_SAMPLE_FMT_FLTP;
		av_frame->sample_rate = sample_rate;
		av_frame->nb_samples = nb_samples;
		av_frame->pts		 = pts;
		::av_channel_layout_default(&av_frame->ch_layout, 2);

		if (::av_frame_get_buffer(av_frame, 0) < 0)
		{
			::av_frame_free(&av_frame);
			return nullptr;
		}

		::av_samples_set_silence(av_frame->extended_data, 0, nb_samples, 2, AV_SAMPLE_FMT_FLTP);

		auto media_frame = ffmpeg::compat::ToMediaFrame(cmn::MediaType::Audio, av_frame);
		::av_frame_free(&av_frame);

		return media_frame;
	}
}  // namespace

// Regression test: an input format change must be applied exactly at its
// boundary frame even when frames of the previous format are still queued.
// The previous flag-based rebuild pushed the queued old-format frames into the
// rebuilt graph, which disabled the audio filter until the next format change.
TEST(TranscodeFilterFormatChange, AudioPropertyChangeWithQueuedBacklog)
{
	auto input_stream_info	= std::make_shared<info::Stream>(StreamSourceType::Ovt);
	auto output_stream_info = std::make_shared<info::Stream>(StreamSourceType::Ovt);

	auto input_track  = MakeAudioTrack(0, 48000);
	auto output_track = MakeAudioTrack(100, 48000);

	std::atomic<int> error_count{0};
	std::atomic<int> output_count{0};

	auto filter = TranscodeFilter::Create(
		0, input_stream_info, input_track, output_stream_info, output_track,
		[&](TranscodeResult result, int32_t id, std::shared_ptr<MediaFrame> frame) {
			if (result == TranscodeResult::DataReady && frame != nullptr)
			{
				output_count++;
			}
			else if (result == TranscodeResult::DataError)
			{
				error_count++;
			}
		});
	ASSERT_NE(filter, nullptr);

	// Burst-enqueue 48kHz frames followed by 44.1kHz frames so that the
	// boundary frame is enqueued while older frames are still in the queue
	int64_t pts = 0;
	for (int i = 0; i < 20; i++)
	{
		filter->SendBuffer(MakeAudioFrame(48000, pts, 1024));
		pts += 1024;
	}
	for (int i = 0; i < 20; i++)
	{
		filter->SendBuffer(MakeAudioFrame(44100, pts, 1024));
		pts += 1024;
	}

	// Both formats resample to 48kHz, so about 40 frames are expected in total
	for (int i = 0; i < 100 && output_count.load() < 30; i++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	filter->Stop();

	EXPECT_EQ(error_count.load(), 0);
	EXPECT_GE(output_count.load(), 30);
}

// Color tag and pixel format changes at the same resolution must also rebuild
// the video filter at the boundary frame, without any error or warning storm
TEST(TranscodeFilterFormatChange, VideoColorTagAndPixelFormatChange)
{
	auto input_stream_info	= std::make_shared<info::Stream>(StreamSourceType::Ovt);
	auto output_stream_info = std::make_shared<info::Stream>(StreamSourceType::Ovt);

	auto input_track  = MakeVideoTrack(0, 640, 360);
	auto output_track = MakeVideoTrack(100, 640, 360);

	std::atomic<int> error_count{0};
	std::atomic<int> output_count{0};

	auto filter = TranscodeFilter::Create(
		0, input_stream_info, input_track, output_stream_info, output_track,
		[&](TranscodeResult result, int32_t id, std::shared_ptr<MediaFrame> frame) {
			if (result == TranscodeResult::DataReady && frame != nullptr)
			{
				output_count++;
			}
			else if (result == TranscodeResult::DataError)
			{
				error_count++;
			}
		});
	ASSERT_NE(filter, nullptr);

	// Untagged frames, then bt709-tagged frames (tag-only change), then 10-bit
	// frames (pixel format change), all at the same resolution
	int64_t pts = 0;
	for (int i = 0; i < 10; i++)
	{
		filter->SendBuffer(MakeVideoFrame(640, 360, AV_PIX_FMT_YUV420P, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, pts));
		pts += 3000;
	}
	for (int i = 0; i < 10; i++)
	{
		filter->SendBuffer(MakeVideoFrame(640, 360, AV_PIX_FMT_YUV420P, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, pts));
		pts += 3000;
	}
	for (int i = 0; i < 10; i++)
	{
		filter->SendBuffer(MakeVideoFrame(640, 360, AV_PIX_FMT_YUV420P10, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, pts));
		pts += 3000;
	}

	for (int i = 0; i < 100 && output_count.load() < 25; i++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	filter->Stop();

	EXPECT_EQ(error_count.load(), 0);
	EXPECT_GE(output_count.load(), 25);
}

// A frame that does not match the graph must be dropped without putting the
// filter into the ERROR state, so the following matching frames keep flowing
TEST(FilterLavfiResampler, MismatchedFrameDoesNotDisableFilter)
{
	auto resampler = std::make_shared<FilterLavfiResampler>();
	resampler->SetInputTrack(MakeAudioTrack(0, 48000));
	resampler->SetOutputTrack(MakeAudioTrack(100, 48000));

	ASSERT_TRUE(resampler->Initialize());

	auto matched = resampler->ProcessFrameInternal(MakeAudioFrame(48000, 0, 1024));
	EXPECT_NE(matched.result, TranscodeResult::DataError);

	auto mismatched = resampler->ProcessFrameInternal(MakeAudioFrame(44100, 1024, 1024));
	EXPECT_EQ(mismatched.result, TranscodeResult::DataError);
	EXPECT_NE(resampler->GetState(), FilterBase::State::ERROR);

	auto recovered = resampler->ProcessFrameInternal(MakeAudioFrame(48000, 2048, 1024));
	EXPECT_NE(recovered.result, TranscodeResult::DataError);
}

TEST(MediaFrameClone, CopiesColorTags)
{
	auto frame = std::make_shared<MediaFrame>();
	frame->SetMediaType(cmn::MediaType::Video);
	frame->SetWidth(1920);
	frame->SetHeight(1080);
	frame->SetColorMatrix(cmn::ColorMatrix::BT709);
	frame->SetColorRange(cmn::ColorRange::Limited);

	auto clone = frame->CloneFrame();
	ASSERT_NE(clone, nullptr);
	EXPECT_EQ(clone->GetColorMatrix(), cmn::ColorMatrix::BT709);
	EXPECT_EQ(clone->GetColorRange(), cmn::ColorRange::Limited);
}
