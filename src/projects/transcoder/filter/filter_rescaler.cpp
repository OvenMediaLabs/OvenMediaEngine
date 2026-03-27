//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "filter_rescaler.h"

#include <base/ovlibrary/ovlibrary.h>

#include "../transcoder_gpu.h"
#include "../transcoder_private.h"
#include "../transcoder_stream_internal.h"

#define MAX_QUEUE_SIZE 2
#define FILTER_FLAG_HWFRAME_AWARE (1 << 0)

FilterRescaler::FilterRescaler()
{
	_frame = ::av_frame_alloc();

	_inputs = ::avfilter_inout_alloc();
	_outputs = ::avfilter_inout_alloc();
	
	_buffersrc = ::avfilter_get_by_name("buffer");
	_buffersink = ::avfilter_get_by_name("buffersink");

	_filter_graph = ::avfilter_graph_alloc();

	_buffersrc_ctx	= nullptr;
	_buffersink_ctx = nullptr;

	OV_ASSERT2(_frame != nullptr);
	OV_ASSERT2(_inputs != nullptr);
	OV_ASSERT2(_outputs != nullptr);
	OV_ASSERT2(_buffersrc != nullptr);
	OV_ASSERT2(_buffersink != nullptr);
	OV_ASSERT2(_filter_graph != nullptr);
	OV_ASSERT2(_buffersrc_ctx == nullptr);
	OV_ASSERT2(_buffersink_ctx == nullptr);

	// Limit the number of filter threads to 4. I think 4 thread is usually enough for video filtering processing.
	_filter_graph->nb_threads = 4;
}

FilterRescaler::~FilterRescaler()
{
	Stop();
}

bool FilterRescaler::InitializeSourceFilter()
{
	std::vector<ov::String> src_params;

	auto resolution = _input_track->GetResolution();
	src_params.push_back(ov::String::FormatString("video_size=%dx%d", resolution.width, resolution.height));
	src_params.push_back(ov::String::FormatString("pix_fmt=%s", ::av_get_pix_fmt_name((AVPixelFormat)_src_pixfmt)));
	src_params.push_back(ov::String::FormatString("time_base=%s", _input_track->GetTimeBase().GetStringExpr().CStr()));
	src_params.push_back(ov::String::FormatString("pixel_aspect=%d/%d", 1, 1));

	_src_args = ov::String::Join(src_params, ":");


	int ret = ::avfilter_graph_create_filter(&_buffersrc_ctx, _buffersrc, "in", _src_args, nullptr, _filter_graph);
	if (ret < 0)
	{
		logte("[%s] Could not create video buffer source filter for rescaling: %s", GetLogPrefix().CStr(), ffmpeg::compat::AVErrorToString(ret).CStr());
		return false;
	}

	_outputs->name = ::av_strdup("in");
	_outputs->filter_ctx = _buffersrc_ctx;
	_outputs->pad_idx = 0;
	_outputs->next = nullptr;

	return true;
}

bool FilterRescaler::InitializeSinkFilter()
{
	int ret = ::avfilter_graph_create_filter(&_buffersink_ctx, _buffersink, "out", nullptr, nullptr, _filter_graph);
	if (ret < 0)
	{
		logte("[%s] Could not create video buffer sink filter for rescaling: %s", GetLogPrefix().CStr(), ffmpeg::compat::AVErrorToString(ret).CStr());
		return false;
	}

	_inputs->name = ::av_strdup("out");
	_inputs->filter_ctx = _buffersink_ctx;
	_inputs->pad_idx = 0;
	_inputs->next = nullptr;

	return true;
}

bool FilterRescaler::InitializeFilterDescription()
{
	std::vector<ov::String> filters;

	if (IsSingleTrack())
	{
		// No need to rescale if the input and output are the same.
	}
	else
	{
		// 2. Timebase
		filters.push_back(ov::String::FormatString("settb=%s", _output_track->GetTimeBase().GetStringExpr().CStr()));

		// 3. Scaler
		auto input_module_id = _input_track->GetCodecModuleId();
		auto input_device_id = _input_track->GetCodecDeviceId();
		auto output_module_id = _output_track->GetCodecModuleId();
		auto output_device_id = _output_track->GetCodecDeviceId();

		// Scaler is performed on the same device as the encoder(output module)
		ov::String desc = "";

		/**
		 * Output Module Cases
		 * - DEFAULT, OPENH264, X264, QSV, LIBVPX, NILOGAN : SW-based module (CPU memory)
		 */
		if (output_module_id == cmn::MediaCodecModuleId::DEFAULT ||
			output_module_id == cmn::MediaCodecModuleId::OPENH264 ||
			output_module_id == cmn::MediaCodecModuleId::X264 ||
			output_module_id == cmn::MediaCodecModuleId::QSV ||
			output_module_id == cmn::MediaCodecModuleId::LIBVPX ||
			// Until now, Logan VPU processes in CPU memory like SW-based modules. Performance needs to be improved in the future
			output_module_id == cmn::MediaCodecModuleId::NILOGAN
			/* || output_module_id == cmn::MediaCodecModuleId::libx26x */)
		{
			switch (input_module_id)
			{
				case cmn::MediaCodecModuleId::NVENC: {
					// Copy data to host memory for cross-device compatibility.
					_src_pixfmt = ffmpeg::compat::GetAVPixelFormatOfHWDevice(input_module_id, input_device_id, true);
					if (_src_pixfmt == AV_PIX_FMT_NONE)
					{
						logte("[%s] Failed to get pixel format for %s(%d)", GetLogPrefix().CStr(), cmn::GetCodecModuleIdString(input_module_id), input_device_id);
						return false;
					}
					_use_hwframe_transfer = true;

					desc.Clear();
				}
				break;
				case cmn::MediaCodecModuleId::XMA: {
					// Copy the frames in Xilinx Device memory to the CPU memory using the xvbm_convert filter.
					desc = ov::String::FormatString("xvbm_convert,");
				}
				break;
				default:
					logtw("Unsupported input module: %s", cmn::GetCodecModuleIdString(input_module_id));
				case cmn::MediaCodecModuleId::X264:
				case cmn::MediaCodecModuleId::QSV:		// CPU memory using 'gpu_copy=on'
				case cmn::MediaCodecModuleId::NILOGAN:	// CPU memory using 'out=sw'
				case cmn::MediaCodecModuleId::DEFAULT:	// CPU memory
				{
					desc.Clear();
				}
			}
			// Scaler description of default module
			auto resolution = _output_track->GetResolution();
			desc += ov::String::FormatString("scale=%dx%d:flags=bilinear", resolution.width, resolution.height);
		}
		/**
		 * Output Module Cases
		 * - NVENC : NVENC (CUDA) HW-based module
		 */		
		else if (output_module_id == cmn::MediaCodecModuleId::NVENC)
		{
			int32_t cuda_id = TranscodeGPU::GetInstance()->GetExternalDeviceId(cmn::MediaCodecModuleId::NVENC, output_device_id);
			
			switch (input_module_id)
			{
				case cmn::MediaCodecModuleId::NVENC: {	
					// Copy data to host memory for cross-device compatibility.
					if (input_device_id != output_device_id)
					{
						_src_pixfmt = ffmpeg::compat::GetAVPixelFormatOfHWDevice(input_module_id, input_device_id, true);
						if (_src_pixfmt == AV_PIX_FMT_NONE)
						{
							logte("[%s] Failed to get pixel format for %s(%d)", GetLogPrefix().CStr(), cmn::GetCodecModuleIdString(input_module_id), input_device_id);
							return false;
						}
						_use_hwframe_transfer = true;

						desc = ov::String::FormatString("hwupload_cuda=device=%d,", cuda_id);
					}
					else
					{
						desc.Clear();
					}
				}
				break;
				case cmn::MediaCodecModuleId::XMA: {
					desc = ov::String::FormatString("xvbm_convert,hwupload_cuda=device=%d,", cuda_id);
				}
				break;
				default:
					logtw("Unsupported input module: %s", cmn::GetCodecModuleIdString(input_module_id));
				case cmn::MediaCodecModuleId::X264:
				case cmn::MediaCodecModuleId::QSV:		// CPU memory using 'gpu_copy=on'
				case cmn::MediaCodecModuleId::NILOGAN:	// CPU memory using 'out=sw'
				case cmn::MediaCodecModuleId::DEFAULT:	// CPU memory
				{
					desc = ov::String::FormatString("hwupload_cuda=device=%d,", cuda_id);
				}
			}
			auto resolution = _output_track->GetResolution();
			desc += ov::String::FormatString("scale_cuda=%d:%d:format=nv12", resolution.width, resolution.height);
		}
		/**
		 * Output Module Cases
		 * - XMA : Xilinx HW-based module
		 */
		else if (output_module_id == cmn::MediaCodecModuleId::XMA)
		{
			// multiscale_xma only supports resolutions multiple of 4.
			auto input_resolution = _input_track->GetResolution();
			bool need_crop_for_multiple_of_4 = (input_resolution.width % 4 != 0 || input_resolution.height % 4 != 0);
			if (need_crop_for_multiple_of_4)
			{
				logtw("[%s] multiscale_xma only supports resolutions multiple of 4. The resolution will be cropped to a multiple of 4.", GetLogPrefix().CStr());
			}

			int32_t desire_width = input_resolution.width - input_resolution.width % 4;
			int32_t desire_height = input_resolution.height - input_resolution.height % 4;

			switch (input_module_id)
			{
				case cmn::MediaCodecModuleId::XMA: {  // Zero Copy
					if (input_device_id != output_device_id)
					{
						desc = ov::String::FormatString("xvbm_convert,");
						if (need_crop_for_multiple_of_4)
						{
							desc += ov::String::FormatString("crop=%d:%d:0:0,", desire_width, desire_height);
						}
					}
					else
					{
						desc.Clear();
						if (need_crop_for_multiple_of_4)
						{
							desc += ov::String::FormatString("xvbm_convert,crop=%d:%d:0:0,", desire_width, desire_height);
						}
					}
				}
				break;
				case cmn::MediaCodecModuleId::NVENC: {
					// Copy data to host memory for cross-device compatibility.
					_src_pixfmt = ffmpeg::compat::GetAVPixelFormatOfHWDevice(input_module_id, input_device_id, true);
					if (_src_pixfmt == AV_PIX_FMT_NONE)
					{
						logte("[%s] Failed to get pixel format for %s(%d)", GetLogPrefix().CStr(), cmn::GetCodecModuleIdString(input_module_id), input_device_id);
						return false;
					}
					_use_hwframe_transfer = true;

					desc.Clear();
					if (need_crop_for_multiple_of_4)
					{
						desc += ov::String::FormatString("crop=%d:%d:0:0,", desire_width, desire_height);
					}
				}
				break;
				default:
					logtw("[%s] Unsupported input module: %s", GetLogPrefix().CStr(), cmn::GetCodecModuleIdString(input_module_id));
				case cmn::MediaCodecModuleId::X264:		// CPU memory
				case cmn::MediaCodecModuleId::QSV:		// CPU memory using 'gpu_copy=on'
				case cmn::MediaCodecModuleId::NILOGAN:	// CPU memory using 'out=sw'
				case cmn::MediaCodecModuleId::DEFAULT:	// CPU memory
				{
					// xvbm_convert is xvbm frame to av frame converter filter
					// desc = ov::String::FormatString("xvbm_convert,");
					desc.Clear();
					if (need_crop_for_multiple_of_4)
					{
						desc += ov::String::FormatString("crop=%d:%d:0:0,", desire_width, desire_height);
					}
				}
			}

			auto output_resolution = _output_track->GetResolution();
			desc += ov::String::FormatString("multiscale_xma=lxlnx_hwdev=%d:outputs=1:out_1_width=%d:out_1_height=%d:out_1_rate=full",
											 _output_track->GetCodecDeviceId(), output_resolution.width, output_resolution.height);
		}
		/**
		 * Output Module Cases
		 * - Unsupported module
		 */		
		else
		{
			logtw("[%s] Unsupported output module id: %d", GetLogPrefix().CStr(), static_cast<int>(output_module_id));
			return false;
		}

		filters.push_back(desc);

		// 4. Pixel Format
		filters.push_back(ov::String::FormatString("format=%s", ::av_get_pix_fmt_name(ffmpeg::compat::ToAVPixelFormat(_output_track->GetColorspace()))));
	}
	
	if(filters.size() == 0)
	{
		filters.push_back("null");
	}

	_filter_desc = ov::String::Join(filters, ",");

	return true;
}

bool FilterRescaler::InitializeFpsFilter()
{
	// Set input parameters
	_fps_filter.SetInputTimebase(_input_track->GetTimeBase());
	_fps_filter.SetInputFrameRate(_input_track->GetFrameRate());

	// Configure skip frames
	int32_t skip_frames_config = _output_track->GetSkipFramesByConfig();
#if _SKIP_FRAMES_ENABLED
	int32_t skip_frames = (skip_frames_config >= 0) ? skip_frames_config : -1;
	_fps_filter.SetSkipFrames(skip_frames);
	
	// If skip frames is enabled, maintain input framerate; otherwise use output framerate
	bool is_skip_enabled = (skip_frames >= 0);
	float output_framerate = is_skip_enabled ? _input_track->GetFrameRate() : _output_track->GetFrameRate();
	_fps_filter.SetOutputFrameRate(output_framerate);
#else
	_fps_filter.SetSkipFrames(-1);  // Disabled
	_fps_filter.SetOutputFrameRate(_output_track->GetFrameRate());
#endif

	// Set frame copy mode based on resolution
	// Use deep copy when resolutions match to prevent in-place modifications by FFmpeg filter graph
	bool same_resolution = (_input_track->GetResolution() == _output_track->GetResolution());

	auto copy_mode = same_resolution ? FilterFps::OutputFrameCopyMode::DeepCopy 
									 : FilterFps::OutputFrameCopyMode::ShallowCopy;
	_fps_filter.SetOutputFrameCopyMode(copy_mode);
	
	return true;
}

bool FilterRescaler::Configure()
{
	SetState(State::CREATED);


	// Initialize source parameters
	auto resolution = _input_track->GetResolution();
	_src_width	  = resolution.width;
	_src_height	  = resolution.height;
	_src_pixfmt	  = ffmpeg::compat::ToAVPixelFormat(_input_track->GetColorspace());

	// Initialize input buffer queue
	_input_buffer.SetThreshold(MAX_QUEUE_SIZE);

	// Initialize FPS filter
	if(InitializeFpsFilter() == false)
	{
		SetState(State::ERROR);

		return false;
	}

	// Initialize the av filter graph
	if (InitializeFilterDescription() == false)
	{
		SetState(State::ERROR);
		
		return false;
	}

	if (InitializeSourceFilter() == false)
	{
		SetState(State::ERROR);
		
		return false;
	}

	if (InitializeSinkFilter() == false)
	{
		SetState(State::ERROR);

		return false;
	}

	SetDescription(ov::String::FormatString("track(#%u -> #%u), module(%s:%d -> %s:%d), params(src:%s -> output:%s), fps(%.2f -> %.2f), skipFrames(%d)",
				   _input_track->GetId(),
				   _output_track->GetId(),
				   cmn::GetCodecModuleIdString(_input_track->GetCodecModuleId()),
				   _input_track->GetCodecDeviceId(),
				   cmn::GetCodecModuleIdString(_output_track->GetCodecModuleId()),
				   _output_track->GetCodecDeviceId(),
				   _src_args.CStr(),
				   _filter_desc.CStr(),
				   _fps_filter.GetInputFrameRate(),
				   _fps_filter.GetOutputFrameRate(),
				   _fps_filter.GetSkipFrames()));

	if ((::avfilter_graph_parse_ptr(_filter_graph, _filter_desc, &_inputs, &_outputs, nullptr)) < 0)
	{
		logte("[%s] Could not parse filter string for rescaling: %s", GetLogPrefix().CStr(), _filter_desc.CStr());
		SetState(State::ERROR);
		
		return false;
	}

	if (SetHWContextToFilterIfNeed() == false)
	{
		logte("[%s] Could not set hw context to filters", GetLogPrefix().CStr());
		SetState(State::ERROR);

		return false;
	}

	if (::avfilter_graph_config(_filter_graph, nullptr) < 0)
	{
		logte("[%s] Could not validate filter graph for rescaling", GetLogPrefix().CStr());
		SetState(State::ERROR);

		return false;
	}

	return true;
}

bool FilterRescaler::Start()
{
	_source_id = ov::Random::GenerateInt32();

	try
	{
		_kill_flag = false;

		auto thread_name = ov::String::FormatString("FLT-rscl-t%u", _output_track->GetId());
		_thread_work = std::thread(&FilterRescaler::WorkerThread, this);
		pthread_setname_np(_thread_work.native_handle(), thread_name.CStr());
		
		if (_codec_init_event.Get() == false)
		{
			_kill_flag = false;

			return false;
		}	
	}
	catch (const std::system_error &e)
	{
		_kill_flag = true;
		SetState(State::ERROR);

		logte("[%s] Failed to start rescaling filter thread", GetLogPrefix().CStr());

		return false;
	}

	return true;
}

void FilterRescaler::Stop()
{
	if(GetState() == State::STOPPED)
		return;

	_kill_flag = true;

	_input_buffer.Stop();

	if (_thread_work.joinable())
	{
		_thread_work.join();
	}

	OV_SAFE_FUNC(_buffersrc_ctx, nullptr, ::avfilter_free, );
	OV_SAFE_FUNC(_buffersink_ctx, nullptr, ::avfilter_free, );
	OV_SAFE_FUNC(_inputs, nullptr, ::avfilter_inout_free, &);
	OV_SAFE_FUNC(_outputs, nullptr, ::avfilter_inout_free, &);
	OV_SAFE_FUNC(_frame, nullptr, ::av_frame_free, &);
	OV_SAFE_FUNC(_filter_graph, nullptr, ::avfilter_graph_free, &);

	_buffersrc= nullptr;
	_buffersink = nullptr;
	
	_input_buffer.Clear();

	_fps_filter.Clear();

	SetState(State::STOPPED);
}

bool FilterRescaler::PushProcess(std::shared_ptr<MediaFrame> media_frame)
{
	if (GetState() == State::ERROR)
	{
		return false;
	}
	
	// Flush the buffer source filter
	if (media_frame == nullptr)
	{
		return false;
	}

	if (media_frame->GetWidth() != _src_width || media_frame->GetHeight() != _src_height)
	{
		logtw("Input frame parameters do not match the expected source parameters. %dx%d (expected: %dx%d)",
			  media_frame->GetWidth(), media_frame->GetHeight(), _src_width, _src_height);

		return false;
	}

	auto src_frame = ffmpeg::compat::ToAVFrame(cmn::MediaType::Video, media_frame);
	if (!src_frame)
	{
		logte("[%s] Could not allocate the video frame data", GetLogPrefix().CStr());

		SetState(State::ERROR);

		return false;
	}

	AVFrame *transfer_frame = nullptr;
	// GPU Memory -> Host Memory
	if (_use_hwframe_transfer == true && src_frame->hw_frames_ctx != nullptr)
	{
		transfer_frame = ::av_frame_alloc();
		if(transfer_frame == nullptr)
		{
			logte("Could not allocate the video frame for hwframe transfer");

			SetState(State::ERROR);

			return false;
		}
		
		if (::av_hwframe_transfer_data(transfer_frame, src_frame, 0) < 0)
		{
			logte("[%s] Error transferring the data to system memory", GetLogPrefix().CStr());

			SetState(State::ERROR);

			return false;
		}

		transfer_frame->pts = src_frame->pts;
	}

	AVFrame *feed_frame = (transfer_frame != nullptr) ? transfer_frame : src_frame;

	// Send to filtergraph
	int ret = ::av_buffersrc_write_frame(_buffersrc_ctx, feed_frame);
	if (ret == AVERROR_EOF)
	{
		logtw("[%s] filter graph has been flushed and will not accept any more frames.", GetLogPrefix().CStr());
	}
	else if (ret == AVERROR(EAGAIN))
	{
		logtw("[%s] filter graph is not able to accept the frame at this time.", GetLogPrefix().CStr());
	}
	else if (ret == AVERROR_INVALIDDATA)
	{
		logtw("[%s] Invalid data while sending to filtergraph", GetLogPrefix().CStr());
	}
	else if (ret == AVERROR(ENOMEM))
	{
		logte("[%s] Could not allocate memory while sending to filtergraph", GetLogPrefix().CStr());
		
		SetState(State::ERROR);

		Complete(TranscodeResult::DataError, nullptr);
		
		return false;
	}
	else if (ret < 0)
	{
		logte("[%s] An error occurred while feeding to filtergraph: format: %d, pts: %" PRId64 ", queue.size: %zu", GetLogPrefix().CStr(), src_frame->format, src_frame->pts, _input_buffer.Size());

		SetState(State::ERROR);

		Complete(TranscodeResult::DataError, nullptr);

		return false;
	}

	// Free the temporary AVFrame used for transfer
	if (transfer_frame != nullptr)
	{
		av_frame_free(&transfer_frame);
	}

	return true;
}

bool FilterRescaler::PopProcess(bool is_flush)
{
	if (GetState() == State::ERROR)
	{
		return false;
	}

	while (!_kill_flag || is_flush)
	{
		// Receive from filtergraph
		int ret = ::av_buffersink_get_frame(_buffersink_ctx, _frame);
		if (ret == AVERROR(EAGAIN))
		{
			break;
		}
		else if (ret == AVERROR_INVALIDDATA)
		{
			logtw("[%s] Invalid data while receiving from filtergraph", GetLogPrefix().CStr());
			break;
		}
		else if (ret == AVERROR_EOF)
		{
			if (is_flush)
				break;

			logte("[%s] Error receiving filtered frame. error(EOF)", GetLogPrefix().CStr());

			SetState(State::ERROR);

			return false;
		}
		else if (ret == AVERROR(ENOMEM))
		{
			logte("[%s] Could not allocate memory while receiving from filtergraph", GetLogPrefix().CStr());

			SetState(State::ERROR);

			Complete(TranscodeResult::DataError, nullptr);

			return false;
		}
		else if (ret < 0)
		{
			if (is_flush)
				break;

			logte("[%s] Error receiving frame from filtergraph. error(%s)", GetLogPrefix().CStr(), ffmpeg::compat::AVErrorToString(ret).CStr());
			
			SetState(State::ERROR);

			Complete(TranscodeResult::DataError, nullptr);

			return false;
		}
		
		_frame->pict_type = AV_PICTURE_TYPE_NONE;
		auto output_frame = ffmpeg::compat::ToMediaFrame(cmn::MediaType::Video, _frame);
		::av_frame_unref(_frame);
		if (output_frame == nullptr)
		{
			continue;
		}

		// Convert duration to output track timebase
		output_frame->SetDuration((int64_t)((double)output_frame->GetDuration() * _input_track->GetTimeBase().GetExpr() / _output_track->GetTimeBase().GetExpr()));
		output_frame->SetSourceId(_source_id);

#if _SIMULATE_PROCESSING_DELAY_ENABLED	
		if ((rand() % 100) == 0)
		{
			_simulate_overload = rand() % 200;
			if(_simulate_overload < 100)
				_simulate_overload = 0;

			logti("[%s] Simulating overload of %d ms for testing", GetLogPrefix().CStr(), _simulate_overload);
			
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(_simulate_overload)); 
#endif

		Complete(TranscodeResult::DataReady, std::move(output_frame));
	}

	return true;
}

#define DO_FILTER_ONCE(frame) \
		if (!PushProcess(frame)) { break; } \
		if (!PopProcess()) { break; } 

#define FLUSH_FILTER_ONCE() \
		{ PushProcess(nullptr); PopProcess(true); }

void FilterRescaler::WorkerThread()
{
	ov::logger::ThreadHelper thread_helper;

	if(_codec_init_event.Submit(Configure()) == false)
	{
		return;
	}

	SetState(State::STARTED);

#if _SKIP_FRAMES_ENABLED
	// Set initial Skip Frames
	_skip_frames_conf			   = _output_track->GetSkipFramesByConfig();
	_skip_frames				   = _skip_frames_conf;
#endif

	// XMA devices expand the memory pool when processing the first frame filtering. 
	// At this time, memory allocation failure occurs because it is not 'Thread safe'. 
	// It is used for the purpose of preventing this.
	bool start_frame_syncronization = true;

	while (!_kill_flag)
	{
		auto obj = _input_buffer.Dequeue();
		if (obj.has_value() == false)
		{
			continue;
		}

		auto media_frame = std::move(obj.value());

		// If the user does not set the output Framerate, use the recommend framerate
		// Cases where the framerate changes dynamically, such as when using WebRTC, WHIP, or SRTP protocols, were considered.
		// It is similar to maintaining the original frame rate.
		if (_output_track->GetFrameRateByConfig() == 0.0f)
		{
			auto recommended_output_framerate = TranscoderStreamInternal::MeasurementToRecommendFramerate(_input_track->GetFrameRate());
			if (_fps_filter.GetOutputFrameRate() != recommended_output_framerate)
			{
				logtd("[%s] Change output framerate. Input: %.2ffps, Output: %.2f -> %.2ffps", GetLogPrefix().CStr(), _input_track->GetFrameRate(), _fps_filter.GetOutputFrameRate(), recommended_output_framerate);
				_fps_filter.SetOutputFrameRate(recommended_output_framerate);
			}
		}

		// If the queue exceeds the threshold, drop the frame.
		// Since the threshold of the input queue has been reduced to 2, this code is no longer necessary. 
		// There is a concern that it may degrade quality, so it will be removed.
		// if (_input_buffer.IsThresholdExceeded())
		// {
		// 	media_frame = nullptr;;
		// }

		if(media_frame != nullptr)
		{
			_fps_filter.Push(media_frame);
		}

		while (auto frame = _fps_filter.Pop())
		{
			if (start_frame_syncronization)
			{
				std::lock_guard<std::mutex> lock(TranscodeGPU::GetInstance()->GetDeviceMutex());

				DO_FILTER_ONCE(frame);

				start_frame_syncronization = false;
			}
			else
			{
				auto start_time = std::chrono::high_resolution_clock::now();
				DO_FILTER_ONCE(frame);
				auto elapsed_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time).count();

				// Update the weighted average frame processing time
				// It includes the time taken for filtering + overlay + delivery to the encoder (including waiting time if there is a load on the encoder).
				_weighted_avg_frame_processing_time_us = (_weighted_avg_frame_processing_time_us * 0.9) + (elapsed_time_us * 0.1); 
			}
		}


#if _SKIP_FRAMES_ENABLED 
		UpdateSkipFrames();
#endif
	}

	// Flush the filter
	FLUSH_FILTER_ONCE();
}

/**
 * Set the hardware device context and hardware frames context to the filter graph if necessary.
 * TODO(Keukhan): Improve the filter graph to set a hardware device context or frame context. current approach doesn’t seem to be correct.
 */
bool FilterRescaler::SetHWContextToFilterIfNeed()
{
	auto hw_device_ctx = TranscodeGPU::GetInstance()->GetDeviceContext(cmn::MediaCodecModuleId::NVENC, _output_track->GetCodecDeviceId());
	if (!hw_device_ctx)
	{
		// No hardware device context
		// This means that the output module is not a hardware module
		return true;
	}

	// Check whether the filter graph contains hwupload_cuda or scale_cuda filter
	bool is_hwupload_cuda = false;
	bool is_scale_cuda	  = false;
	for (uint32_t i = 0; i < _filter_graph->nb_filters; i++)
	{
		auto filter = _filter_graph->filters[i];
		if ((filter == nullptr) || (filter->filter->flags_internal & FILTER_FLAG_HWFRAME_AWARE) == 0)
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

	// Set the hardware device context and hardware frames context to the filter graph
	for (uint32_t i = 0; i < _filter_graph->nb_filters; i++)
	{
		auto filter = _filter_graph->filters[i];

		if ((filter == nullptr) || ((filter->filter->flags_internal & FILTER_FLAG_HWFRAME_AWARE) == 0) || (filter->inputs == nullptr))
		{
			continue;
		}

		bool matched = false;
		// TODO(Keukhan): scale_npp is deprecated. Remove it in the future.
		if (strstr(filter->name, "scale_cuda") != nullptr || strstr(filter->name, "hwupload_cuda") != nullptr)
		{
			matched = true;
		}

		if (matched == true)
		{
			if (is_hwupload_cuda == true || is_scale_cuda == true)
			{
				if (ffmpeg::compat::SetHwDeviceCtxOfAVFilterContext(filter, hw_device_ctx) == false)
				{
					logte("[%s] Could not set hw device context for %s", GetLogPrefix().CStr(), filter->name);
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

					auto resolution = _output_track->GetResolution();
					if (ffmpeg::compat::SetHWFramesCtxOfAVFilterLink(input, hw_device_ctx, resolution.width, resolution.height) == false)
					{
						logte("[%s] Could not set hw frames context for %s", GetLogPrefix().CStr(), filter->name);
						return false;
					}
				}
			}
		}
	}

	return true;
}

#if _SKIP_FRAMES_ENABLED 
#define _SKIP_FRAMES_EVALUATION_INTERVAL_MS 	1000	// 1s
#define _SKIP_FRAMES_RECOVERY_HOLD_INTERVAL_MS 	5000	// 5s
#define _SKIP_FRAMES_ENSURE_FPS_MARGIN_RATIO 	0.9f	// 90%

void FilterRescaler::UpdateSkipFrames()
{
	// Skip frame is disabled.
	if (_skip_frames_conf < 0)
	{
		return;
	}

	// Static skip frames set by the user.
	if (_skip_frames_conf > 0)
	{
		if (_fps_filter.GetSkipFrames() != _skip_frames_conf)
		{
			_fps_filter.SetSkipFrames(_skip_frames_conf);
			logti("[%s] Changed skip frames to user config value: %d", GetLogPrefix().CStr(), _fps_filter.GetSkipFrames());
		}
		return;
	}

	auto curr_time = ov::Time::GetTimestampInMs();

	if (_skip_frames_last_check_time == 0 || _skip_frames_last_changed_time == 0)
	{
		_skip_frames_last_check_time   = curr_time;
		_skip_frames_last_changed_time = curr_time;
	}

	auto elapsed_check_time = curr_time - _skip_frames_last_check_time;
	auto elapsed_stable_time = curr_time - _skip_frames_last_changed_time;

	// Checking every 1 second is sufficient for skip frame adjustment
	if (elapsed_check_time <= _SKIP_FRAMES_EVALUATION_INTERVAL_MS)
	{
		return;
	}
	_skip_frames_last_check_time = curr_time;

	// Remain for debugging and future improvement for queue-based skip frame adjustment
	// -----------------------------------------------------------------------------
	// double actual_input_fps			   = _fps_filter.GetInputFramesPerSecond();
	// double expected_input_fps		   = _fps_filter.GetInputFrameRate();

	// double expected_output_fps		   = _fps_filter.GetExpectedOutputFramesPerSecond();

	// int64_t queue_waiting_deviation_us = _input_buffer.GetWaitingTimeInUs();
	// double expected_frame_interval_us  = (expected_input_fps > 0.0) ? (1000000.0 / expected_input_fps) : 0.0;
	// bool is_queue_overload			   = (expected_frame_interval_us > 0.0) &&
	// 						 (queue_waiting_deviation_us > expected_frame_interval_us * _SKIP_FRAMES_QUEUE_BACKLOG_RATIO);
	// bool is_queue_stable = (expected_frame_interval_us > 0.0) &&
	// 					   (queue_waiting_deviation_us < expected_frame_interval_us * _SKIP_FRAMES_QUEUE_RECOVERY_RATIO);

	double fixed_output_fps			   = _fps_filter.GetOutputFrameRate();
	double expected_output_fps		   = _fps_filter.GetExpectedOutputFramesPerSecond();
	double actual_output_fps		   = _fps_filter.GetOutputFramesPerSecond();

	if (_weighted_avg_frame_processing_time_us <= 0.0 || fixed_output_fps <= 0.0)
	{
		return;
	}

	// Calculate the maximum possible frames per second.
	double max_frames_per_second = (1000000.0 / _weighted_avg_frame_processing_time_us);
	// To ensure stability, set a margin and use OO% of the calculated maximum FPS.
	double ideal_frames_per_second = max_frames_per_second * _SKIP_FRAMES_ENSURE_FPS_MARGIN_RATIO;

	if (ideal_frames_per_second <= 0.0)
	{
		// If the ideal FPS is not a positive value, skip frame cannot be performed.
		return;
	}

	// Calculate number of skip frames value to match the ideal FPS.
	auto next_skip_frames = static_cast<int32_t>(std::ceil(fixed_output_fps / ideal_frames_per_second - 1.0));
	if (next_skip_frames > fixed_output_fps - 1)
	{
		next_skip_frames = static_cast<int32_t>(std::floor(fixed_output_fps - 1));
	}
	else if (next_skip_frames < 0)
	{
		next_skip_frames = 0;
	}

	ov::String common_log = ov::String::FormatString("Possible FPS: %.2f/%.2f(ideal), Output FPS: %.2f/%.2f/%.2f", max_frames_per_second, ideal_frames_per_second, fixed_output_fps, expected_output_fps, actual_output_fps);

	// Increase skip frames immediately when bottleneck occurs.
	if (_skip_frames < next_skip_frames)
	{
		logtw("[%s] Changed SkipFrames %d -> %d (Bottleneck). %s", GetLogPrefix().CStr(), _skip_frames, next_skip_frames, common_log.CStr());

		_skip_frames = next_skip_frames;
		_fps_filter.SetSkipFrames(_skip_frames);

		_skip_frames_last_changed_time = curr_time;
	}
	// Decrease skip frames slowly when the system is recovering.
	else if ((_skip_frames > next_skip_frames))
	{
		if (elapsed_stable_time > _SKIP_FRAMES_RECOVERY_HOLD_INTERVAL_MS)
		{
			// Decay 20% per step (rate-limited)
			int32_t rate_limited_next = _skip_frames - std::max(1, _skip_frames / 5); 
			next_skip_frames = std::max(rate_limited_next, next_skip_frames);
			if (next_skip_frames < 0)
			{
				next_skip_frames = 0;
			}

			logti("[%s] Changed SkipFrames %d -> %d (Recovery). %s", GetLogPrefix().CStr(), _skip_frames, next_skip_frames, common_log.CStr());

			_skip_frames = next_skip_frames;
			_fps_filter.SetSkipFrames(_skip_frames);

			_skip_frames_last_changed_time = curr_time;
		}
		else
		{
			logtt("[%s] Hold SkipFrames %d (Waiting for recovery). %s", GetLogPrefix().CStr(), _skip_frames, common_log.CStr());
		}
	}
	// Keep skip frames unchanged when the system is stable.
	else
	{
		logtt("[%s] Unchanged SkipFrames %d (Stable). %s", GetLogPrefix().CStr(), _skip_frames, common_log.CStr());
	}
}
#endif // _SKIP_FRAMES_ENABLED