//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <algorithm>
#include <stdint.h>
#include <memory>
#include <thread>
#include <vector>

#include "../codec/codec_base.h"
#include "../transcoder_context.h"

#include <base/info/application.h>
#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>
#include <modules/ffmpeg/compat.h>
#include <modules/managed_queue/managed_queue.h>

class FilterBase
{
public:
	enum class State : uint8_t {
		CREATED,
		STARTED,
		STOPPED,
		ERROR
	};

	typedef std::function<void(TranscodeResult, std::shared_ptr<MediaFrame>)> CompleteHandler;
	FilterBase() = default;
	virtual ~FilterBase() = default;

	virtual bool Configure(const std::shared_ptr<MediaTrack> &input_track, const std::shared_ptr<MediaTrack> &output_track) = 0;
	virtual bool Start() = 0;
	virtual void Stop() = 0;
	
	cmn::Timebase GetInputTimebase() const
	{
		return _input_track->GetTimeBase();
	}

	cmn::Timebase GetOutputTimebase() const
	{
		return _output_track->GetTimeBase();
	}

	int32_t GetInputWidth() const 
	{
		return _src_width;
	}

	int32_t GetInputHeight() const 
	{
		return _src_height;
	}

	// 	If the input track and output track are the same, the filter is used for a single track.
	// The main goal of this filter is to handle frame drops.
	bool IsSingleTrack() const
	{
		return (_input_track == _output_track)? true : false;
	}

	void SetCompleteHandler(CompleteHandler complete_handler) {
		_complete_handler = complete_handler;
	}

	// Set URN for the filter buffer queue
	void SetQueueUrn(std::shared_ptr<info::ManagedQueue::URN> &urn) {	
		_input_buffer.SetUrn(urn);
	}

	// Set queue policy
	void SetQueuePolicy(bool exceed_wait_enable, size_t threshold = 0) {
		_input_buffer.SetExceedWaitEnable(exceed_wait_enable);
		_input_buffer.SetThreshold(threshold);
	}

	void SetState(State state)
	{
		_state = state;
	}

	State GetState() const
	{
		return _state;
	}

	bool SendBuffer(std::shared_ptr<MediaFrame> buffer)
	{
		if(GetState() == State::CREATED || GetState() == State::STARTED)
		{
			_input_buffer.Enqueue(std::move(buffer));

			return true;
		}

		return false;
	}

	void Complete(TranscodeResult result, std::shared_ptr<MediaFrame> buffer)
	{
		if (_complete_handler != nullptr && _kill_flag == false)
		{
			_complete_handler(result, std::move(buffer));
		}
	}

	void SetInputTrack(std::shared_ptr<MediaTrack> input_track)
	{
		_input_track = input_track;
	}

	void SetOutputTrack(std::shared_ptr<MediaTrack> output_track)
	{
		_output_track = output_track;
	}

	int32_t GetBufferSize() const
	{
		return _input_buffer.Size();
	}

	void SetDescription(const ov::String &description)
	{
		_description = description;
	}

	ov::String GetDescription() const
	{
		return _description;
	}

protected:
	std::atomic<State> _state = State::CREATED;

	ov::ManagedQueue<std::shared_ptr<MediaFrame>> _input_buffer;

	AVFrame *_frame = nullptr;

	int32_t 	_src_pixfmt = 0;
	int32_t 	_src_width = 0;
	int32_t 	_src_height = 0;

	ov::String 	_src_args = "";
	
	ov::String 	_filter_desc = "";
	ov::String 	_description = "";

	AVFilterContext *_buffersink_ctx = nullptr;
	AVFilterContext *_buffersrc_ctx = nullptr;
	AVFilterGraph *_filter_graph = nullptr;
	AVFilterInOut *_inputs = nullptr;
	AVFilterInOut *_outputs = nullptr;

	const AVFilter *_buffersrc = nullptr;
	const AVFilter *_buffersink = nullptr;

	ov::Future _codec_init_event;

	// resolution of the input video frame
	std::shared_ptr<MediaTrack> _input_track;
	std::shared_ptr<MediaTrack> _output_track;

	std::atomic<bool> _kill_flag{false};
	std::thread _thread_work;

	CompleteHandler _complete_handler;

	bool _use_hwframe_transfer = false;

	int32_t _source_id = 0;
};
