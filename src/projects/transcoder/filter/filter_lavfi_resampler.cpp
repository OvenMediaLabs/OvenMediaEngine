//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "filter_lavfi_resampler.h"

#include <base/ovlibrary/ovlibrary.h>

#include "../transcoder_private.h"

FilterLavfiResampler::~FilterLavfiResampler()
{
	Uninitialize();
}

void FilterLavfiResampler::Uninitialize()
{
	_graph.Release();
}

bool FilterLavfiResampler::InitializeSourceFilter()
{
	std::vector<ov::String> src_params = {
		ov::String::FormatString("time_base=%s", _input_track->GetTimeBase().GetStringExpr().CStr()),
		ov::String::FormatString("sample_rate=%d", _input_track->GetSampleRate()),
		ov::String::FormatString("sample_fmt=%s", _input_track->GetSample().GetName()),
		ov::String::FormatString("channel_layout=%s", _input_track->GetChannel().GetName())};

	_src_args = ov::String::Join(src_params, ":");

	if (_graph.CreateBufferSource(_src_args) == false)
	{
		logte("Could not create audio buffer source filter for resampling: %s", _graph.GetLastErrorString().CStr());

		return false;
	}

	return true;
}

bool FilterLavfiResampler::InitializeSinkFilter()
{
	if (_graph.CreateBufferSink() == false)
	{
		logte("Could not create audio buffer sink filter for resampling: %s", _graph.GetLastErrorString().CStr());

		return false;
	}

	return true;
}

bool FilterLavfiResampler::InitializeFilterDescription()
{
	std::vector<ov::String> filters;

	// SW -> SW
	if(IsSingleTrack())
	{
		filters.push_back(ov::String::FormatString("aresample=async=1"));
		filters.push_back(ov::String::FormatString("asetnsamples=n=%d", _output_track->GetAudioSamplesPerFrame()));
	}
	else
	{
		filters.push_back(ov::String::FormatString("asettb=%s", _output_track->GetTimeBase().GetStringExpr().CStr()));
		filters.push_back(ov::String::FormatString("aresample=%d", _output_track->GetSampleRate()));
		filters.push_back(ov::String::FormatString("aformat=sample_fmts=%s:channel_layouts=%s", _output_track->GetSample().GetName(), _output_track->GetChannel().GetName()));
	}

	if (filters.size() == 0)
	{
		filters.push_back("null");
	}

	_filter_desc = ov::String::Join(filters, ",");

	return true;
}

bool FilterLavfiResampler::Initialize()
{
	SetState(State::CREATED);

	// Allocate the av filter graph (limit to 1 thread; usually enough for audio filtering)
	if (_graph.Alloc(cmn::MediaType::Audio, 1) == false)
	{
		logte("Could not allocate the filter graph for resampling");
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

	if (InitializeFilterDescription() == false)
	{
		SetState(State::ERROR);

		return false;
	}

	SetDescription(ov::String::FormatString("track(#%u -> #%u), params(src: %s -> output: %s)",
				   _input_track->GetId(),
				   _output_track->GetId(),
				   _src_args.CStr(),
				   _filter_desc.CStr()));

	if (_graph.Parse(_filter_desc) == false)
	{
		logte("Could not parse filter string for resampling: %s", _filter_desc.CStr());
		SetState(State::ERROR);
		return false;
	}

	if (_graph.Config() == false)
	{
		logte("Could not validate filter graph for resampling: %s", _graph.GetLastErrorString().CStr());
		SetState(State::ERROR);

		return false;
	}

	SetState(State::STARTED);

	return true;
}

bool FilterLavfiResampler::SendFrame(std::shared_ptr<MediaFrame> media_frame)
{
	if (GetState() == State::ERROR)
	{
		return false;
	}

	ffmpeg::CodecResult result = _graph.PushFrame(media_frame, false);
	if (result == ffmpeg::CodecResult::NoMemory)
	{
		logte("Could not allocate the frame data");
		SetState(State::ERROR);

		return false;
	}
	else if (result != ffmpeg::CodecResult::Ok)
	{
		logte("An error occurred while feeding the audio filtergraph: %s", _graph.GetLastErrorString().CStr());
		SetState(State::ERROR);

		return false;
	}

	return true;
}

std::shared_ptr<MediaFrame> FilterLavfiResampler::ReceiveFrame()
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
		else if (recv.result != ffmpeg::CodecResult::Ok)
		{
			logte("Error receiving filtered frame. error(%s)", _graph.GetLastErrorString().CStr());
			SetState(State::ERROR);

			return nullptr;
		}

		auto output_frame = recv.frame;
		if (output_frame == nullptr)
		{
			logte("Could not allocate the frame data");

			// Keep draining; nothing to report for this frame.
			continue;
		}

		output_frame->SetSourceId(_source_id);

		return output_frame;
	}
}
