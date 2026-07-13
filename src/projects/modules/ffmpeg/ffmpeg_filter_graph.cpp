//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#include "ffmpeg_filter_graph.h"

namespace ffmpeg
{
	FFmpegFilterGraph::~FFmpegFilterGraph()
	{
		Release();
	}

	bool FFmpegFilterGraph::Alloc(cmn::MediaType media_type, int nb_threads)
	{
		_media_type = media_type;

		_frame	 = ::av_frame_alloc();
		_inputs	 = ::avfilter_inout_alloc();
		_outputs = ::avfilter_inout_alloc();

		const bool is_video = (media_type == cmn::MediaType::Video);
		_buffersrc			= ::avfilter_get_by_name(is_video ? "buffer" : "abuffer");
		_buffersink			= ::avfilter_get_by_name(is_video ? "buffersink" : "abuffersink");

		_filter_graph = ::avfilter_graph_alloc();

		if (_frame == nullptr || _inputs == nullptr || _outputs == nullptr ||
			_buffersrc == nullptr || _buffersink == nullptr || _filter_graph == nullptr)
		{
			return false;
		}

		_filter_graph->nb_threads = nb_threads;

		return true;
	}

	bool FFmpegFilterGraph::CreateBufferSource(const ov::String &args)
	{
		_last_error = ::avfilter_graph_create_filter(&_buffersrc_ctx, _buffersrc, "in", args.CStr(), nullptr, _filter_graph);
		if (_last_error < 0)
		{
			return false;
		}

		_outputs->name		 = ::av_strdup("in");
		_outputs->filter_ctx = _buffersrc_ctx;
		_outputs->pad_idx	 = 0;
		_outputs->next		 = nullptr;

		return true;
	}

	bool FFmpegFilterGraph::CreateBufferSink()
	{
		_last_error = ::avfilter_graph_create_filter(&_buffersink_ctx, _buffersink, "out", nullptr, nullptr, _filter_graph);
		if (_last_error < 0)
		{
			return false;
		}

		_inputs->name		= ::av_strdup("out");
		_inputs->filter_ctx = _buffersink_ctx;
		_inputs->pad_idx	= 0;
		_inputs->next		= nullptr;

		return true;
	}

	bool FFmpegFilterGraph::Parse(const ov::String &filter_desc)
	{
		_last_error = ::avfilter_graph_parse_ptr(_filter_graph, filter_desc.CStr(), &_inputs, &_outputs, nullptr);
		return _last_error >= 0;
	}

	bool FFmpegFilterGraph::Config()
	{
		_last_error = ::avfilter_graph_config(_filter_graph, nullptr);
		return _last_error >= 0;
	}

	CodecResult FFmpegFilterGraph::PushFrame(const std::shared_ptr<const MediaFrame> &media_frame, bool hwframe_transfer)
	{
		// If media_frame is nullptr, it indicates the end of the stream.
		if(media_frame == nullptr)
		{
			return ToCodecResult(::av_buffersrc_write_frame(_buffersrc_ctx, nullptr));
		}

		const auto &data = media_frame->GetData();
		if(data == nullptr)
		{
			return CodecResult::InvalidData;
		}

		std::shared_ptr<MediaFrameData> host_holder;
		AVFrame *src_frame	 = nullptr;
		AVFrame *local_frame = nullptr;	 // built from generic host planes; freed below

		// GPU to CPU transfer
		if (hwframe_transfer == false && data->GetBackend() == MediaFrameData::Backend::FFmpeg)
		{
			// Software / already-host FFmpeg frame: use its AVFrame directly.
			src_frame = static_cast<AVFrame *>(data->GetNativeHandle());
		}
		else if (hwframe_transfer == true && data->IsHardwareFrame())
		{
			// Download to host memory(CPU)
			host_holder = data->DownloadToHost();
			if (host_holder == nullptr)
			{
				return CodecResult::NoMemory;
			}

			if (host_holder->GetBackend() == MediaFrameData::Backend::FFmpeg)
			{
				// Reuse the AVFrame
				src_frame = static_cast<AVFrame *>(host_holder->GetNativeHandle());
			}
			else
			{
				// Build a new AVFrame from the generic host planes.
				// For future non-FFmpeg backends; such a backend must override
				// GetPixelFormat()/GetPlaneCount()/GetPlaneData()/GetStride().
				local_frame = ::av_frame_alloc();
				if (local_frame == nullptr)
				{
					return CodecResult::NoMemory;
				}

				local_frame->format = static_cast<int>(compat::ToAVPixelFormat(host_holder->GetPixelFormat()));
				local_frame->width	= media_frame->GetWidth();
				local_frame->height = media_frame->GetHeight();

				// Copy the host planes into an owned (reference-counted) buffer so the
				// data stays valid after host_holder is released, and the filter graph
				// can reference the frame without copying it again.
				if (::av_frame_get_buffer(local_frame, 0) < 0)
				{
					::av_frame_free(&local_frame);
					return CodecResult::NoMemory;
				}

				// Validate before copying the host planes into the AVFrame.
				int planes = ::av_pix_fmt_count_planes(static_cast<AVPixelFormat>(local_frame->format));
				if (planes <= 0 || host_holder->GetPlaneCount() != planes)
				{
					::av_frame_free(&local_frame);
					return CodecResult::InvalidData;
				}

				const uint8_t *src_data[AV_NUM_DATA_POINTERS] = { nullptr };
				int src_linesize[AV_NUM_DATA_POINTERS]		  = { 0 };

				for (int i = 0; i < planes && i < AV_NUM_DATA_POINTERS; i++)
				{
					src_data[i]		= host_holder->GetPlaneData(i);
					src_linesize[i] = host_holder->GetStride(i);

					if (src_data[i] == nullptr || src_linesize[i] <= 0)
					{
						::av_frame_free(&local_frame);
						return CodecResult::InvalidData;
					}
				}

				::av_image_copy(local_frame->data, local_frame->linesize,
								src_data, src_linesize,
								static_cast<AVPixelFormat>(local_frame->format),
								local_frame->width, local_frame->height);

				local_frame->pts = media_frame->GetPts();
				src_frame		 = local_frame;
			}
		}

		if (src_frame == nullptr)
		{
			if (local_frame != nullptr)
			{
				::av_frame_free(&local_frame);
			}
			return CodecResult::NoMemory;
		}

		CodecResult result = ToCodecResult(::av_buffersrc_write_frame(_buffersrc_ctx, src_frame));

		if (local_frame != nullptr)
		{
			::av_frame_free(&local_frame);
		}

		return result;
	}

	ReceiveResult FFmpegFilterGraph::PullFrame()
	{
		CodecResult result = ToCodecResult(::av_buffersink_get_frame(_buffersink_ctx, _frame));
		if (result != CodecResult::Ok)
		{
			return { result, nullptr };
		}

		if (_media_type == cmn::MediaType::Video)
		{
			_frame->pict_type = AV_PICTURE_TYPE_NONE;
		}

		auto media_frame = compat::ToMediaFrame(_media_type, _frame);
		::av_frame_unref(_frame);

		return { CodecResult::Ok, std::move(media_frame) };
	}

	void FFmpegFilterGraph::Release()
	{
		OV_SAFE_FUNC(_buffersrc_ctx, nullptr, ::avfilter_free, );
		OV_SAFE_FUNC(_buffersink_ctx, nullptr, ::avfilter_free, );
		OV_SAFE_FUNC(_inputs, nullptr, ::avfilter_inout_free, &);
		OV_SAFE_FUNC(_outputs, nullptr, ::avfilter_inout_free, &);
		OV_SAFE_FUNC(_frame, nullptr, ::av_frame_free, &);
		OV_SAFE_FUNC(_filter_graph, nullptr, ::avfilter_graph_free, &);

		_buffersrc	= nullptr;
		_buffersink = nullptr;
	}

	ov::String FFmpegFilterGraph::GetLastErrorString() const
	{
		return compat::AVErrorToString(_last_error);
	}

	bool FFmpegFilterGraph::ApplyCudaHwContext(const std::shared_ptr<HwDeviceContext> &device_ctx, int32_t width, int32_t height)
	{
		if (device_ctx == nullptr || _filter_graph == nullptr)
		{
			return false;
		}

		auto hw_device_ctx = static_cast<AVBufferRef *>(device_ctx->GetNativeHandle());
		if (hw_device_ctx == nullptr)
		{
			return false;
		}

		// libavfilter's internal "hardware-frame aware" flag (not exported in the public headers).
		constexpr int kFilterFlagHwframeAware = (1 << 0);

		// Detect which CUDA filters the parsed graph contains.
		bool is_hwupload_cuda = false;
		bool is_scale_cuda	  = false;
		for (uint32_t i = 0; i < _filter_graph->nb_filters; i++)
		{
			auto filter = _filter_graph->filters[i];
			if ((filter == nullptr) || (filter->filter->flags_internal & kFilterFlagHwframeAware) == 0)
			{
				continue;
			}

			if (strstr(filter->name, "scale_cuda") != nullptr)
			{
				is_scale_cuda = true;
			}
			else if (strstr(filter->name, "hwupload_cuda") != nullptr)
			{
				is_hwupload_cuda = true;
			}
		}

		// Apply the device/frames context to the matching filters.
		for (uint32_t i = 0; i < _filter_graph->nb_filters; i++)
		{
			auto filter = _filter_graph->filters[i];
			if ((filter == nullptr) || ((filter->filter->flags_internal & kFilterFlagHwframeAware) == 0) || (filter->inputs == nullptr))
			{
				continue;
			}

			if (strstr(filter->name, "scale_cuda") == nullptr && strstr(filter->name, "hwupload_cuda") == nullptr)
			{
				continue;
			}

			if (is_hwupload_cuda == true || is_scale_cuda == true)
			{
				if (SetHwDeviceContextOfFilter(filter, hw_device_ctx) == false)
				{
					loge("FFmpegFilterGraph", "Could not set hw device context for %s", filter->name);
					return false;
				}
			}
		}

		if (is_hwupload_cuda == false && is_scale_cuda == true)
		{
			if (SetHwFramesContextOfBufferSource(hw_device_ctx, width, height) == false)
			{
				loge("FFmpegFilterGraph", "Could not set hw frames context on the buffer source");
				return false;
			}
		}

		return true;
	}

	bool FFmpegFilterGraph::SetHwDeviceContextOfFilter(AVFilterContext *filter, AVBufferRef *hw_device_ctx)
	{
		filter->hw_device_ctx = ::av_buffer_ref(hw_device_ctx);
		return filter->hw_device_ctx != nullptr;
	}

	bool FFmpegFilterGraph::SetHwFramesContextOfBufferSource(AVBufferRef *hw_device_ctx, int32_t width, int32_t height)
	{
		if (_buffersrc_ctx == nullptr)
		{
			return false;
		}

		AVBufferRef *hw_frames_ref = ::av_hwframe_ctx_alloc(hw_device_ctx);
		if (hw_frames_ref == nullptr)
		{
			return false;
		}

		auto constraints = ::av_hwdevice_get_hwframe_constraints(hw_device_ctx, nullptr);
		if (constraints == nullptr)
		{
			::av_buffer_unref(&hw_frames_ref);
			return false;
		}

		auto frames_ctx			   = reinterpret_cast<AVHWFramesContext *>(hw_frames_ref->data);
		frames_ctx->format		   = *(constraints->valid_hw_formats);
		frames_ctx->sw_format	   = *(constraints->valid_sw_formats);
		frames_ctx->width		   = width;
		frames_ctx->height		   = height;
		frames_ctx->initial_pool_size = 2;

		::av_hwframe_constraints_free(&constraints);

		if (::av_hwframe_ctx_init(hw_frames_ref) < 0)
		{
			::av_buffer_unref(&hw_frames_ref);
			return false;
		}

		// Attach the frames context (and matching hw pixel format) to the buffer source.
		AVBufferSrcParameters *params = ::av_buffersrc_parameters_alloc();
		if (params == nullptr)
		{
			::av_buffer_unref(&hw_frames_ref);
			return false;
		}

		params->format		  = frames_ctx->format;
		params->width		  = width;
		params->height		  = height;
		params->hw_frames_ctx = hw_frames_ref;

		int ret = ::av_buffersrc_parameters_set(_buffersrc_ctx, params);
		::av_free(params);
		// av_buffersrc_parameters_set() took its own reference, so release ours.
		::av_buffer_unref(&hw_frames_ref);

		return ret >= 0;
	}

	CodecResult FFmpegFilterGraph::ToCodecResult(int error)
	{
		_last_error = error;

		if (error == 0)
		{
			return CodecResult::Ok;
		}
		if (error == AVERROR(EAGAIN))
		{
			return CodecResult::Again;
		}
		if (error == AVERROR_EOF)
		{
			return CodecResult::Eof;
		}
		if (error == AVERROR_INVALIDDATA)
		{
			return CodecResult::InvalidData;
		}
		if (error == AVERROR(ENOMEM))
		{
			return CodecResult::NoMemory;
		}

		return CodecResult::Error;
	}
}  // namespace ffmpeg
