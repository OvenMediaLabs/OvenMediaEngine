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

		std::shared_ptr<MediaFrameData> host_holder;
		AVFrame *src_frame = static_cast<AVFrame *>(media_frame->GetPrivData());

		// GPU to CPU transfer 
		if (hwframe_transfer && data != nullptr && data->IsHardwareFrame())
		{
			host_holder = data->DownloadToHost();
			if (host_holder == nullptr)
			{
				return CodecResult::NoMemory;
			}

			src_frame = static_cast<AVFrame *>(host_holder->GetNativeHandle());
		}

		if (src_frame == nullptr)
		{
			return CodecResult::NoMemory;
		}

		return ToCodecResult(::av_buffersrc_write_frame(_buffersrc_ctx, src_frame));
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

			if (is_hwupload_cuda == false && is_scale_cuda == true)
			{
				for (uint32_t j = 0; j < filter->nb_inputs; j++)
				{
					auto input = filter->inputs[j];
					if (input == nullptr)
					{
						continue;
					}

					if (SetHwFramesContextOfFilterLink(input, hw_device_ctx, width, height) == false)
					{
						loge("FFmpegFilterGraph", "Could not set hw frames context for %s", filter->name);
						return false;
					}
				}
			}
		}

		return true;
	}

	bool FFmpegFilterGraph::SetHwDeviceContextOfFilter(AVFilterContext *filter, AVBufferRef *hw_device_ctx)
	{
		filter->hw_device_ctx = ::av_buffer_ref(hw_device_ctx);
		return filter->hw_device_ctx != nullptr;
	}

	bool FFmpegFilterGraph::SetHwFramesContextOfFilterLink(AVFilterLink *link, AVBufferRef *hw_device_ctx, int32_t width, int32_t height)
	{
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

		link->hw_frames_ctx = hw_frames_ref;

		return true;
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
