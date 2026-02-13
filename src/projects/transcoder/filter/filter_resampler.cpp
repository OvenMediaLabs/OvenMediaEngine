//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "filter_resampler.h"

#include <base/ovlibrary/ovlibrary.h>

#include "../transcoder_private.h"

FilterResampler::FilterResampler()
{
	_frame = ::av_frame_alloc();

	_inputs = ::avfilter_inout_alloc();
	_outputs = ::avfilter_inout_alloc();

	_buffersrc = ::avfilter_get_by_name("abuffer");
	_buffersink = ::avfilter_get_by_name("abuffersink");

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

	// Limit the number of filter threads to 1. I think 1 thread is usually enough for audio filtering processing.
	_filter_graph->nb_threads = 1;
}

FilterResampler::~FilterResampler()
{
	Stop();

	OV_SAFE_FUNC(_frame, nullptr, ::av_frame_free, &);
	OV_SAFE_FUNC(_inputs, nullptr, ::avfilter_inout_free, &);
	OV_SAFE_FUNC(_outputs, nullptr, ::avfilter_inout_free, &);
	OV_SAFE_FUNC(_filter_graph, nullptr, ::avfilter_graph_free, &);

	_buffersrc= nullptr;
	_buffersink = nullptr;

	_input_buffer.Clear();
}

bool FilterResampler::InitializeSourceFilter()
{
	std::vector<ov::String> src_params = {
		ov::String::FormatString("time_base=%s", _input_track->GetTimeBase().GetStringExpr().CStr()),
		ov::String::FormatString("sample_rate=%d", _input_track->GetSampleRate()),
		ov::String::FormatString("sample_fmt=%s", _input_track->GetSample().GetName()),
		ov::String::FormatString("channel_layout=%s", _input_track->GetChannel().GetName())};

	_src_args = ov::String::Join(src_params, ":");

	int ret = ::avfilter_graph_create_filter(&_buffersrc_ctx, _buffersrc, "in", _src_args, nullptr, _filter_graph);
	if (ret < 0)
	{
		logte("Could not create audio buffer source filter for resampling: %d", ret);

		return false;
	}

	_outputs->name = ::av_strdup("in");
	_outputs->filter_ctx = _buffersrc_ctx;
	_outputs->pad_idx = 0;
	_outputs->next = nullptr;

	return true;
}

bool FilterResampler::InitializeSinkFilter()
{
	int ret = ::avfilter_graph_create_filter(&_buffersink_ctx, _buffersink, "out", nullptr, nullptr, _filter_graph);
	if (ret < 0)
	{
		logte("Coud not create audio buffer sink filter for resampling: %d", ret);

		return false;
	}

	_inputs->name = ::av_strdup("out");
	_inputs->filter_ctx = _buffersink_ctx;
	_inputs->pad_idx = 0;
	_inputs->next = nullptr;

	return true;
}

bool FilterResampler::InitializeFilterDescription()
{
	std::vector<ov::String> filters;

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

bool FilterResampler::Configure(const std::shared_ptr<MediaTrack> &input_track, const std::shared_ptr<MediaTrack> &output_track)
{
	int ret;

	SetState(State::CREATED);

	_input_track = input_track;
	_output_track = output_track;

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

	if ((_outputs->name == nullptr) || (_inputs->name == nullptr))
	{
		SetState(State::ERROR);

		return false;
	}

	SetDescription(ov::String::FormatString("track(#%u -> #%u), params(src: %s -> output: %s)",
				   _input_track->GetId(),
				   _output_track->GetId(),
				   _src_args.CStr(),
				   _filter_desc.CStr()));

	if (::avfilter_graph_parse_ptr(_filter_graph, _filter_desc, &_inputs, &_outputs, nullptr) < 0)
	{
		logte("Could not parse filter string for resampling: %s", _filter_desc.CStr());
		SetState(State::ERROR);
		return false;
	}

	if ((ret = ::avfilter_graph_config(_filter_graph, nullptr)) < 0)
	{
		logte("Could not validate filter graph for resampling: %d", ret);
		SetState(State::ERROR);

		return false;
	}

	return true;
}

bool FilterResampler::Start()
{
	_source_id = ov::Random::GenerateInt32();

	try
	{
		_kill_flag = false;

		_thread_work = std::thread(&FilterResampler::WorkerThread, this);
		pthread_setname_np(_thread_work.native_handle(), ov::String::FormatString("FLT-rsmp-t%u", _output_track->GetId()).CStr());
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

		logte("Failed to start resample filter thread.");

		return false;
	}

	return true;
}

void FilterResampler::Stop()
{
	_kill_flag = true;

	_input_buffer.Stop();

	if (_thread_work.joinable())
	{
		_thread_work.join();

		logtt("filter resampler thread has ended");
	}

	SetState(State::STOPPED);
}

void FilterResampler::WorkerThread()
{
	ov::logger::ThreadHelper thread_helper;

	auto result = Configure(_input_track, _output_track);
	if (_codec_init_event.Submit(result) == false)
	{
		return;
	}

	int ret;

	SetState(State::STARTED);

	while (!_kill_flag)
	{
		auto obj = _input_buffer.Dequeue();
		if (obj.has_value() == false)
		{
			continue;
		}

		auto media_frame = std::move(obj.value());

		auto av_frame = ffmpeg::compat::ToAVFrame(cmn::MediaType::Video, media_frame);
		if (!av_frame)
		{
			logte("Could not allocate the frame data");

			SetState(State::ERROR);

			break;
		}

		// logtw("Resampled in frame. pts: %lld, linesize: %d, samples: %d", av_frame->pts, av_frame->linesize[0], av_frame->nb_samples);

		ret = ::av_buffersrc_write_frame(_buffersrc_ctx, av_frame);
		if (ret < 0)
		{
			logte("An error occurred while feeding the audio filtergraph: pts: %lld, linesize: %d, srate: %d, channels: %d, format: %d",
				  av_frame->pts, av_frame->linesize[0], av_frame->sample_rate, av_frame->ch_layout.nb_channels, av_frame->format);

			Complete(TranscodeResult::DataError, nullptr);
			
			continue;
		}

		while (!_kill_flag)
		{
			int ret = ::av_buffersink_get_frame(_buffersink_ctx, _frame);

			if (ret == AVERROR(EAGAIN))
			{
				break;
			}
			else if (ret == AVERROR_EOF)
			{
				logte("Error receiving filtered frame. error(EOF)");

				SetState(State::ERROR);

				break;
			}
			else if (ret < 0)
			{
				logte("Error receiving filtered frame. error(%s)", ffmpeg::compat::AVErrorToString(ret).CStr());

				SetState(State::ERROR);

				Complete(TranscodeResult::DataError, nullptr);

				break;
			}
			else
			{
				// logti("Resampled out frame. pts: %lld, linesize: %d, samples : %d", _frame->pts, _frame->linesize[0], _frame->nb_samples);
				auto output_frame = ffmpeg::compat::ToMediaFrame(cmn::MediaType::Audio, _frame);
				::av_frame_unref(_frame);
				if (output_frame == nullptr)
				{
					logte("Could not allocate the frame data");

					continue;
				}

				output_frame->SetSourceId(_source_id);

				Complete(TranscodeResult::DataReady, std::move(output_frame));
			}
		}
	}
}
