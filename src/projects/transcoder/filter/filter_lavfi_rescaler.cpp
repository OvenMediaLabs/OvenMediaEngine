//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "filter_lavfi_rescaler.h"

#include <base/ovlibrary/ovlibrary.h>

#include "../transcoder_private.h"

FilterLavfiRescaler::~FilterLavfiRescaler()
{
	Uninitialize();
}

/**
 * Output Module Cases
 * - DEFAULT, OPENH264, X264, QSV, LIBVPX, NILOGAN : SW-based module (CPU memory)
 *
 * The scaler runs in host memory with the `scale` filter. Input frames that
 * live in device memory are brought down to the host first (NVENC via hwframe
 * transfer, XMA via xvbm_convert).
 */
bool FilterLavfiRescaler::BuildDescription(ov::String &desc)
{
	auto input_module_id = _input_track->GetCodecModuleId();
	auto input_device_id = _input_track->GetCodecDeviceId();

	// SW -> SW
	// HW -> SW (Download to host memory)
	switch (input_module_id)
	{
		// HW -> SW
		case cmn::MediaCodecModuleId::NVENC: {
			// Copy data to host memory for cross-device compatibility.
			_src_pixfmt = ffmpeg::compat::GetVideoPixelFormatOfHWDevice(input_module_id, input_device_id, true);
			if (_src_pixfmt == cmn::VideoPixelFormatId::None)
			{
				logte("[%s] Failed to get pixel format for %s(%d)", GetLogPrefix().CStr(), cmn::GetCodecModuleIdString(input_module_id), input_device_id);
				return false;
			}
			_use_hwframe_transfer = true;
			desc.Clear();
		}
		break;
		case cmn::MediaCodecModuleId::XMA: {
			desc = ov::String::FormatString("xvbm_convert,");
		}
		break;
		case cmn::MediaCodecModuleId::DEFAULT:	// CPU memory
		{
			desc.Clear();
		}
		break;
		case cmn::MediaCodecModuleId::NILOGAN:	
		default: {
			logtw("Unsupported input module: %s", cmn::GetCodecModuleIdString(input_module_id));
			desc.Clear();
		}
	}

	// Scaler description of default module
	auto resolution = _output_track->GetResolution();
	desc += ov::String::FormatString("scale=%dx%d:flags=bilinear", resolution.width, resolution.height);

	return true;
}

bool FilterLavfiRescaler::InitializeSourceFilter()
{
	std::vector<ov::String> src_params;

	auto resolution = _input_track->GetResolution();
	src_params.push_back(ov::String::FormatString("video_size=%dx%d", resolution.width, resolution.height));
	src_params.push_back(ov::String::FormatString("pix_fmt=%s", ffmpeg::compat::GetAVPixelFormatName(ffmpeg::compat::ToAVPixelFormat(_src_pixfmt)).CStr()));
	src_params.push_back(ov::String::FormatString("time_base=%s", _input_track->GetTimeBase().GetStringExpr().CStr()));
	src_params.push_back(ov::String::FormatString("pixel_aspect=%d/%d", 1, 1));

	_src_args = ov::String::Join(src_params, ":");

	if (_graph.CreateBufferSource(_src_args) == false)
	{
		logte("[%s] Could not create video buffer source filter for rescaling: %s", GetLogPrefix().CStr(), _graph.GetLastErrorString().CStr());
		return false;
	}

	return true;
}

bool FilterLavfiRescaler::InitializeSinkFilter()
{
	if (_graph.CreateBufferSink() == false)
	{
		logte("[%s] Could not create video buffer sink filter for rescaling: %s", GetLogPrefix().CStr(), _graph.GetLastErrorString().CStr());
		return false;
	}

	return true;
}

bool FilterLavfiRescaler::InitializeFilterDescription()
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
		ov::String desc = "";
		if (BuildDescription(desc) == false)
		{
			return false;
		}
		filters.push_back(desc);

		// 4. Pixel Format
		filters.push_back(ov::String::FormatString("format=%s", ffmpeg::compat::GetAVPixelFormatName(ffmpeg::compat::ToAVPixelFormat(_output_track->GetColorspace())).CStr()));
	}

	if (filters.size() == 0)
	{
		filters.push_back("null");
	}

	_filter_desc = ov::String::Join(filters, ",");

	return true;
}

bool FilterLavfiRescaler::Initialize()
{
	SetState(State::CREATED);

	// Initialize source parameters
	auto resolution = _input_track->GetResolution();
	_src_width	  = resolution.width;
	_src_height	  = resolution.height;
	_src_pixfmt	  = _input_track->GetColorspace();

	// Initialize FPS filter
	if (InitializeFpsFilter() == false)
	{
		SetState(State::ERROR);

		return false;
	}

	// Allocate the av filter graph (limit to 4 threads; usually enough for video filtering)
	if (_graph.Alloc(cmn::MediaType::Video, 4) == false)
	{
		logte("[%s] Could not allocate the filter graph for rescaling", GetLogPrefix().CStr());
		SetState(State::ERROR);

		return false;
	}

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

	if (_graph.Parse(_filter_desc) == false)
	{
		logte("[%s] Could not parse filter string for rescaling: %s", GetLogPrefix().CStr(), _filter_desc.CStr());
		SetState(State::ERROR);

		return false;
	}

	if (_graph.Config() == false)
	{
		logte("[%s] Could not validate filter graph for rescaling: %s", GetLogPrefix().CStr(), _graph.GetLastErrorString().CStr());
		SetState(State::ERROR);

		return false;
	}

#if _SKIP_FRAMES_ENABLED
	// Set initial Skip Frames
	_skip_frames_conf = _output_track->GetSkipFramesByConfig();
	_skip_frames	  = _skip_frames_conf;
#endif

	_is_first_frame = true;

	SetState(State::STARTED);

	return true;
}

void FilterLavfiRescaler::Uninitialize()
{
	if (GetState() == State::STOPPED)
		return;

	_graph.Release();

	_fps_filter.Clear();

	SetState(State::STOPPED);
}

bool FilterLavfiRescaler::SendFrame(std::shared_ptr<MediaFrame> media_frame)
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

	// PushFrame transfers the frame from GPU to host memory when _use_hwframe_transfer is set.
	ffmpeg::CodecResult result = _graph.PushFrame(media_frame, _use_hwframe_transfer);

	switch (result)
	{
		case ffmpeg::CodecResult::Ok:
			return true;
		case ffmpeg::CodecResult::Eof:
			logtw("[%s] filter graph has been flushed and will not accept any more frames.", GetLogPrefix().CStr());
			return true;
		case ffmpeg::CodecResult::Again:
			logtw("[%s] filter graph is not able to accept the frame at this time.", GetLogPrefix().CStr());
			return true;
		case ffmpeg::CodecResult::InvalidData:
			logtw("[%s] Invalid data while sending to filtergraph", GetLogPrefix().CStr());
			return true;
		case ffmpeg::CodecResult::NoMemory:
			logte("[%s] Could not allocate memory while sending to filtergraph", GetLogPrefix().CStr());
			SetState(State::ERROR);
			return false;
		case ffmpeg::CodecResult::Error:
		default:
			logte("[%s] An error occurred while feeding to filtergraph: %s", GetLogPrefix().CStr(), _graph.GetLastErrorString().CStr());
			SetState(State::ERROR);
			return false;
	}
}

std::shared_ptr<MediaFrame> FilterLavfiRescaler::ReceiveFrame()
{
	if (GetState() == State::ERROR)
	{
		return nullptr;
	}

	// Receive one frame from filtergraph. Loops only to skip frames that fail conversion.
	while (true)
	{
		auto recv = _graph.PullFrame();

		if (recv.result == ffmpeg::CodecResult::Again || recv.result == ffmpeg::CodecResult::Eof)
		{
			return nullptr;
		}
		else if (recv.result == ffmpeg::CodecResult::InvalidData)
		{
			logtw("[%s] Invalid data while receiving from filtergraph", GetLogPrefix().CStr());
			return nullptr;
		}
		else if (recv.result == ffmpeg::CodecResult::NoMemory)
		{
			logte("[%s] Could not allocate memory while receiving from filtergraph", GetLogPrefix().CStr());
			SetState(State::ERROR);
			return nullptr;
		}
		else if (recv.result != ffmpeg::CodecResult::Ok)
		{
			logte("[%s] Error receiving frame from filtergraph. error(%s)", GetLogPrefix().CStr(), _graph.GetLastErrorString().CStr());
			SetState(State::ERROR);
			return nullptr;
		}

		auto output_frame = recv.frame;
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
			if (_simulate_overload < 100)
				_simulate_overload = 0;

			logti("[%s] Simulating overload of %d ms for testing", GetLogPrefix().CStr(), _simulate_overload);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(_simulate_overload));
#endif

		return output_frame;
	}
}
