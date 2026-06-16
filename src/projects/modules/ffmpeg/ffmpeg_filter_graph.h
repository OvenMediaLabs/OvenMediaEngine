//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/ffmpeg/compat.h>
#include <modules/ffmpeg/ffmpeg_codec.h>  // CodecResult, ReceiveResult

#include <cstring>

extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/frame.h>
}

namespace ffmpeg
{
	class FFmpegFilterGraph
	{
	public:
		FFmpegFilterGraph() = default;

		~FFmpegFilterGraph();

		// Non-copyable and non-movable: always used as a direct member, never moved or stored by value.
		FFmpegFilterGraph(const FFmpegFilterGraph &)			= delete;
		FFmpegFilterGraph &operator=(const FFmpegFilterGraph &) = delete;
		FFmpegFilterGraph(FFmpegFilterGraph &&)					= delete;
		FFmpegFilterGraph &operator=(FFmpegFilterGraph &&)		= delete;

		// Allocates the graph and the reusable frame, and selects the (a)buffer / (a)buffersink filters
		// for the given media type. nb_threads bounds the graph's worker threads.
		bool Alloc(cmn::MediaType media_type, int nb_threads);

		// Creates the "in" buffer source filter from the given arguments and links it as the graph output.
		bool CreateBufferSource(const ov::String &args);

		// Creates the "out" buffer sink filter and links it as the graph input.
		bool CreateBufferSink();

		// Parses the filter description string and links it between the source/sink.
		bool Parse(const ov::String &filter_desc);

		// Validates/configures the parsed graph.
		bool Config();

		CodecResult PushFrame(const std::shared_ptr<const MediaFrame> &media_frame, bool hwframe_transfer);

		ReceiveResult PullFrame();

		void Release();

		// Human-readable description of the most recent avfilter error.
		ov::String GetLastErrorString() const;

		// Injects the given hardware device context into the parsed graph's CUDA-aware filters
		// (hwupload_cuda / scale_cuda), and a hardware frames context into scale_cuda's input link when
		// no hwupload precedes it. The device context is owned by the caller.
		bool ApplyCudaHwContext(const std::shared_ptr<HwDeviceContext> &device_ctx, int32_t width, int32_t height);

	private:
		// Attaches a new reference to hw_device_ctx onto the given CUDA-aware filter.
		static bool SetHwDeviceContextOfFilter(AVFilterContext *filter, AVBufferRef *hw_device_ctx);

		// Allocates a hardware frames context from hw_device_ctx and attaches it to the given
		// filter link's input (used for scale_cuda when no hwupload precedes it).
		static bool SetHwFramesContextOfFilterLink(AVFilterLink *link, AVBufferRef *hw_device_ctx, int32_t width, int32_t height);

		// Maps an FFmpeg avfilter return code to CodecResult, remembering the raw error for
		// GetLastErrorString().
		CodecResult ToCodecResult(int error);

		cmn::MediaType _media_type = cmn::MediaType::Unknown;

		AVFrame *_frame				   = nullptr;  // Reused output frame used by PullFrame().
		AVFilterContext *_buffersrc_ctx	  = nullptr;
		AVFilterContext *_buffersink_ctx  = nullptr;
		AVFilterGraph *_filter_graph	  = nullptr;
		AVFilterInOut *_inputs			  = nullptr;
		AVFilterInOut *_outputs			  = nullptr;
		const AVFilter *_buffersrc		  = nullptr;
		const AVFilter *_buffersink		  = nullptr;

		int _last_error = 0;
	};
}  // namespace ffmpeg
