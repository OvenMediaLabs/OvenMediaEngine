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

#define _SKIP_FRAMES_ENABLED 1
#define _SKIP_FRAMES_CHECK_INTERVAL 2000				 // 2s
#define _SKIP_FRAMES_STABLE_FOR_RETRIEVE_INTERVAL 10000	 // 10s

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

	src_params.push_back(ov::String::FormatString("video_size=%dx%d", _input_track->GetWidth(), _input_track->GetHeight()));
	src_params.push_back(ov::String::FormatString("pix_fmt=%s", ::av_get_pix_fmt_name((AVPixelFormat)_src_pixfmt)));
	src_params.push_back(ov::String::FormatString("time_base=%s", _input_track->GetTimeBase().GetStringExpr().CStr()));
	src_params.push_back(ov::String::FormatString("pixel_aspect=%d/%d", 1, 1));

	_src_args = ov::String::Join(src_params, ":");


	int ret = ::avfilter_graph_create_filter(&_buffersrc_ctx, _buffersrc, "in", _src_args, nullptr, _filter_graph);
	if (ret < 0)
	{
		logte("Could not create video buffer source filter for rescaling: %d", ret);
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
		logte("Could not create video buffer sink filter for rescaling: %d", ret);
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
						logte("Failed to get pixel format for %s(%d)", cmn::GetCodecModuleIdString(input_module_id), input_device_id);
						return false;
					}
					_use_hwframe_transfer = true;

					desc = ov::String::FormatString("");
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
					desc = ov::String::FormatString("");
				}
			}
			// Scaler description of default module
			desc += ov::String::FormatString("scale=%dx%d:flags=bilinear", _output_track->GetWidth(), _output_track->GetHeight());
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
							logte("Failed to get pixel format for %s(%d)", cmn::GetCodecModuleIdString(input_module_id), input_device_id);
							return false;
						}
						_use_hwframe_transfer = true;

						desc = ov::String::FormatString("hwupload_cuda=device=%d,", cuda_id);
					}
					else
					{
						desc = ov::String::FormatString("");
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
			desc += ov::String::FormatString("scale_cuda=%d:%d:format=nv12", _output_track->GetWidth(), _output_track->GetHeight());
		}
		/**
		 * Output Module Cases
		 * - XMA : Xilinx HW-based module
		 */
		else if (output_module_id == cmn::MediaCodecModuleId::XMA)
		{
			// multiscale_xma only supports resolutions multiple of 4.
			bool need_crop_for_multiple_of_4 = (_input_track->GetHeight() % 4 != 0 || _input_track->GetHeight() % 4 != 0);
			if (need_crop_for_multiple_of_4)
			{
				logtw("multiscale_xma only supports resolutions multiple of 4. The resolution will be cropped to a multiple of 4.");
			}

			int32_t desire_width = _input_track->GetWidth() - _input_track->GetWidth() % 4;
			int32_t desire_height = _input_track->GetHeight() - _input_track->GetHeight() % 4;

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
						desc = ov::String::FormatString("");
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
						logte("Failed to get pixel format for %s(%d)", cmn::GetCodecModuleIdString(input_module_id), input_device_id);
						return false;
					}
					_use_hwframe_transfer = true;

					desc = ov::String::FormatString("");
					if (need_crop_for_multiple_of_4)
					{
						desc += ov::String::FormatString("crop=%d:%d:0:0,", desire_width, desire_height);
					}
				}
				break;
				default:
					logtw("Unsupported input module: %s", cmn::GetCodecModuleIdString(input_module_id));
				case cmn::MediaCodecModuleId::X264:		// CPU memory
				case cmn::MediaCodecModuleId::QSV:		// CPU memory using 'gpu_copy=on'
				case cmn::MediaCodecModuleId::NILOGAN:	// CPU memory using 'out=sw'
				case cmn::MediaCodecModuleId::DEFAULT:	// CPU memory
				{
					// xvbm_convert is xvbm frame to av frame converter filter
					// desc = ov::String::FormatString("xvbm_convert,");
					desc = ov::String::FormatString("");
					if (need_crop_for_multiple_of_4)
					{
						desc += ov::String::FormatString("crop=%d:%d:0:0,", desire_width, desire_height);
					}
				}
			}

			desc += ov::String::FormatString("multiscale_xma=lxlnx_hwdev=%d:outputs=1:out_1_width=%d:out_1_height=%d:out_1_rate=full",
											 _output_track->GetCodecDeviceId(), _output_track->GetWidth(), _output_track->GetHeight());
		}
		/**
		 * Output Module Cases
		 * - Unsupported module
		 */		
		else
		{
			logtw("Unsupported output module id: %d", output_module_id);
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
	bool same_resolution = (_input_track->GetWidth() == _output_track->GetWidth() &&
							_input_track->GetHeight() == _output_track->GetHeight());
	
	auto copy_mode = same_resolution ? FilterFps::OutputFrameCopyMode::DeepCopy 
									 : FilterFps::OutputFrameCopyMode::ShallowCopy;
	_fps_filter.SetOutputFrameCopyMode(copy_mode);
	
	return true;
}

bool FilterRescaler::Configure(const std::shared_ptr<MediaTrack> &input_track, const std::shared_ptr<MediaTrack> &output_track)
{
	SetState(State::CREATED);

	_input_track = input_track;
	_output_track = output_track;

	// Initialize source parameters
	_src_width	  = _input_track->GetWidth();
	_src_height	  = _input_track->GetHeight();
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
		logte("Could not parse filter string for rescaling: %s", _filter_desc.CStr());
		SetState(State::ERROR);
		
		return false;
	}

	if (SetHWContextToFilterIfNeed() == false)
	{
		logte("Could not set hw context to filters");
		SetState(State::ERROR);

		return false;
	}

	if (::avfilter_graph_config(_filter_graph, nullptr) < 0)
	{
		logte("Could not validate filter graph for rescaling");
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

		_thread_work = std::thread(&FilterRescaler::WorkerThread, this);
		pthread_setname_np(_thread_work.native_handle(), ov::String::FormatString("FLT-rscl-t%u", _output_track->GetId()).CStr());
		
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

		logte("Failed to start rescaling filter thread");

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

	auto src_frame = ffmpeg::compat::ToAVFrame(cmn::MediaType::Video, media_frame);
	if (!src_frame)
	{
		logte("Could not allocate the video frame data");

		SetState(State::ERROR);

		return false;
	}

	AVFrame *transfer_frame = nullptr;
	// GPU Memory -> Host Memory
	if (_use_hwframe_transfer == true && src_frame->hw_frames_ctx != nullptr)
	{
		transfer_frame = ::av_frame_alloc();
		if (::av_hwframe_transfer_data(transfer_frame, src_frame, 0) < 0)
		{
			logte("Error transferring the data to system memory\n");

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
		logtw("filter graph has been flushed and will not accept any more frames.");
	}
	else if (ret == AVERROR(EAGAIN))
	{
		logtw("filter graph is not able to accept the frame at this time.");
	}
	else if (ret == AVERROR_INVALIDDATA)
	{
		logtw("Invalid data while sending to filtergraph");
	}
	else if (ret == AVERROR(ENOMEM))
	{
		logte("Could not allocate memory while sending to filtergraph");
		
		SetState(State::ERROR);

		Complete(TranscodeResult::DataError, nullptr);
		
		return false;
	}
	else if (ret < 0)
	{
		logte("An error occurred while feeding to filtergraph: format: %d, pts: %lld, queue.size: %d", src_frame->format, src_frame->pts, _input_buffer.Size());

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
			logtw("Invalid data while receiving from filtergraph");
			break;
		}
		else if (ret == AVERROR_EOF)
		{
			if (is_flush)
				break;

			logte("Error receiving filtered frame. error(EOF)");

			SetState(State::ERROR);

			return false;
		}
		else if (ret == AVERROR(ENOMEM))
		{
			logte("Could not allocate memory while receiving from filtergraph");

			SetState(State::ERROR);

			Complete(TranscodeResult::DataError, nullptr);

			return false;
		}
		else if (ret < 0)
		{
			if (is_flush)
				break;

			logte("Error receiving frame from filtergraph. error(%s)", ffmpeg::compat::AVErrorToString(ret).CStr());
			
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

	if(_codec_init_event.Submit(Configure(_input_track, _output_track)) == false)
	{
		return;
	}

	SetState(State::STARTED);

#if _SKIP_FRAMES_ENABLED
	auto skip_frames_last_check_time = ov::Time::GetTimestampInMs();
	auto skip_frames_last_changed_time = ov::Time::GetTimestampInMs();
	

	// Set initial Skip Frames
	int32_t skip_frames_conf = _output_track->GetSkipFramesByConfig();
	int32_t skip_frames = skip_frames_conf;
	
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

#if _SKIP_FRAMES_ENABLED 
		// Dynamic skip frames adjustment
		if (skip_frames_conf == 0)
		{
			auto curr_time			 = ov::Time::GetTimestampInMs();

			// Periodically check the status of the queue
			auto elapsed_check_time	 = curr_time - skip_frames_last_check_time;
			auto elapsed_stable_time = curr_time - skip_frames_last_changed_time;

			auto threshold_rate		 = 0.75f;
			if (elapsed_check_time > _SKIP_FRAMES_CHECK_INTERVAL)
			{
				skip_frames_last_check_time = curr_time;

				logtt("SkipFrames(%d), Current FPS(%.2f), Expected FPS(%.2f), Threshold FPS(%.2f), Queue(%d/%d)",
					  skip_frames,
					  _fps_filter.GetOutputFramesPerSecond(),
					  _fps_filter.GetExpectedOutputFramesPerSecond(),
					  _fps_filter.GetExpectedOutputFramesPerSecond() * threshold_rate,
					  _input_buffer.GetSize(),
					  _input_buffer.GetThreshold());

				// If the queue is unstable, quickly increase the number of skip frames.
				// Actual output fps is less than 75% of expected fps
				if (_fps_filter.GetOutputFramesPerSecond() < (_fps_filter.GetExpectedOutputFramesPerSecond() * threshold_rate))
				{
					skip_frames_last_changed_time = curr_time;

					// The frame skip should not be more than 1 second.
					int32_t max_skip_frames		  = (int32_t)(_fps_filter.GetExpectedOutputFramesPerSecond() - 1);
					if (++skip_frames > max_skip_frames)
					{
						skip_frames = max_skip_frames;
					}

					logtw("Scaler is unstable. changing skip frames %d to %d", skip_frames - 1, skip_frames);
				}
				// If the queue is stable, slowly decrease the number of skip frames.
				else if ((skip_frames > 0) && (elapsed_stable_time > _SKIP_FRAMES_STABLE_FOR_RETRIEVE_INTERVAL))
				{
					// Queue is stable when there is only 1 or no frame in the queue
					if (_input_buffer.GetSize() <= 1)
					{
						skip_frames_last_changed_time = curr_time;

						if (--skip_frames < 0)
						{
							skip_frames = 0;
						}

						logti("Scaler is stable. changing skip frames %d to %d", skip_frames + 1, skip_frames);
					}
				}

				_fps_filter.SetSkipFrames(skip_frames);
			}
		}
		// Static skip frames set by the user
		else if (skip_frames_conf > 0)
		{
			if (_fps_filter.GetSkipFrames() != skip_frames_conf)
			{
				_fps_filter.SetSkipFrames(skip_frames_conf);

				logti("Changed skip frames to user config value: %d", _fps_filter.GetSkipFrames());
			}
		}
		else  // if (skip_frames_conf < 0)
		{
			// Disable skip frames
		}

		// If the user does not set the output Framerate, use the recommend framerate
		// Cases where the framerate changes dynamically, such as when using WebRTC, WHIP, or SRTP protocols, were considered.
		// It is similar to maintaining the original frame rate.
		if (_output_track->GetFrameRateByConfig() == 0.0f)
		{
			auto recommended_output_framerate = TranscoderStreamInternal::MeasurementToRecommendFramerate(_input_track->GetFrameRate());
			if (_fps_filter.GetOutputFrameRate() != recommended_output_framerate)
			{
				logtt("Change output framerate. Input: %.2ffps, Output: %.2f -> %.2ffps", _input_track->GetFrameRate(), _fps_filter.GetOutputFrameRate(), recommended_output_framerate);
				_fps_filter.SetOutputFrameRate(recommended_output_framerate);
			}
		}

		// If the queue exceeds the threshold, drop the frame.
		if (_input_buffer.IsThresholdExceeded())
		{
			media_frame = nullptr;;
		}

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
				DO_FILTER_ONCE(frame);
			}
		}
#else
		if (start_frame_syncronization)
		{
			std::lock_guard<std::mutex> lock(TranscodeGPU::GetInstance()->GetDeviceMutex());

			DO_FILTER_ONCE(media_frame);

			start_frame_syncronization = false;
		}
		else
		{
			DO_FILTER_ONCE(media_frame);
		}
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
					logte("Could not set hw device context for %s", filter->name);
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

					if (ffmpeg::compat::SetHWFramesCtxOfAVFilterLink(input, hw_device_ctx, _output_track->GetWidth(), _output_track->GetHeight()) == false)
					{
						logte("Could not set hw frames context for %s", filter->name);
						return false;
					}
				}
			}
		}
	}

	return true;
}
