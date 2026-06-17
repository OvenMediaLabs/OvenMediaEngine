//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#include "ffmpeg_media_frame.h"

namespace ffmpeg
{
	FFmpegMediaFrameData::FFmpegMediaFrameData(AVFrame *frame)
		: _frame(frame)
	{
	}

	FFmpegMediaFrameData::~FFmpegMediaFrameData()
	{
		if (_frame != nullptr)
		{
			::av_frame_unref(_frame);
			::av_frame_free(&_frame);
		}
	}

	MediaFrameData::Backend FFmpegMediaFrameData::GetBackend() const
	{
		return Backend::FFmpeg;
	}

	void *FFmpegMediaFrameData::GetNativeHandle() const
	{
		return _frame;
	}

	std::shared_ptr<MediaFrameData> FFmpegMediaFrameData::Clone(bool deep) const
	{
		if (_frame == nullptr)
		{
			return nullptr;
		}

		// Create a new frame that references the same buffer as the source.
		AVFrame *cloned = ::av_frame_clone(_frame);
		if (cloned == nullptr)
		{
			return nullptr;
		}

		if (deep == true)
		{
			// Detach from the shared buffer so the copy can be modified in-place.
 			if (::av_frame_make_writable(cloned) < 0)
 			{
 				::av_frame_free(&cloned);
 				return nullptr;
 			}
		}

		return std::make_shared<FFmpegMediaFrameData>(cloned);
	}

	bool FFmpegMediaFrameData::IsHardwareFrame() const
	{
		return _frame != nullptr && _frame->hw_frames_ctx != nullptr;
	}

	std::shared_ptr<MediaFrameData> FFmpegMediaFrameData::DownloadToHost() const
	{
		if (_frame == nullptr || _frame->hw_frames_ctx == nullptr)
		{
			return nullptr;
		}

		AVFrame *host = ::av_frame_alloc();
		if (host == nullptr)
		{
			return nullptr;
		}

		// GPU memory -> host memory
		if (::av_hwframe_transfer_data(host, _frame, 0) < 0)
		{
			::av_frame_free(&host);
			return nullptr;
		}

		host->pts = _frame->pts;

		return std::make_shared<FFmpegMediaFrameData>(host);
	}

	void FFmpegMediaFrameData::FillZero()
	{
		if (_frame == nullptr)
		{
			return;
		}

		if (IsHardwareFrame() == true)
		{
			return;
		}

		// Audio frame
		if (_frame->nb_samples > 0 && _frame->ch_layout.nb_channels > 0)
		{
			// Writes the correct per-format silence value (e.g. 0x80 for U8) across every (planar) channel plane.
			::av_samples_set_silence(_frame->extended_data, 0, _frame->nb_samples,
									 _frame->ch_layout.nb_channels,
									 static_cast<AVSampleFormat>(_frame->format));
			return;
		}

		// Video frame
		if (_frame->width > 0 && _frame->height > 0)
		{
			const AVPixFmtDescriptor *desc = ::av_pix_fmt_desc_get(static_cast<AVPixelFormat>(_frame->format));

			for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
			{
				if (_frame->data[i] == nullptr || _frame->linesize[i] == 0)
				{
					continue;
				}

				int plane_height = _frame->height;
				if (desc != nullptr && (i == 1 || i == 2))
				{
					plane_height = AV_CEIL_RSHIFT(_frame->height, desc->log2_chroma_h);
				}

				const int stride = std::abs(_frame->linesize[i]);
				::memset(_frame->data[i], 0, static_cast<size_t>(stride) * plane_height);
			}
		}
	}

	void FFmpegMediaFrameData::SetPts(int64_t pts)
	{
		if (_frame != nullptr)
		{
			_frame->pts = pts;
			_frame->pkt_dts = pts;
		}
	}

	void FFmpegMediaFrameData::SetDuration(int64_t duration)
	{
		if (_frame != nullptr)
		{
			_frame->pkt_duration = duration;
		}
	}

	void FFmpegMediaFrameData::SetNbSamples(int32_t nb_samples)
	{
		if (_frame != nullptr)
		{
			_frame->nb_samples = nb_samples;
		}
	}
}  // namespace ffmpeg
