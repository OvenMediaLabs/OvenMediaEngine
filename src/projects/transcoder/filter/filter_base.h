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
#include <vector>

#include "../codec/codec_base.h"
#include "../media_frame.h"

#include <base/info/application.h>
#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>

struct FilterResult
{
	TranscodeResult result = TranscodeResult::Again;
	std::shared_ptr<MediaFrame> frame = nullptr;
	ov::String error = "";

	static FilterResult NoOutput()
	{
		return { TranscodeResult::Again, nullptr };
	}

	static FilterResult Ready(std::shared_ptr<MediaFrame> frame)
	{
		return { TranscodeResult::DataReady, std::move(frame) };
	}

	static FilterResult Error(ov::String error = "")
	{
		return { TranscodeResult::DataError, nullptr, error };
	}
};

class FilterBase
{
public:
	enum class State : uint8_t {
		CREATED,
		STARTED,
		STOPPED,
		ERROR
	};

	FilterBase() = default;
	virtual ~FilterBase() = default;

	virtual bool Initialize() = 0;
	virtual void Uninitialize() = 0;
	virtual FilterResult ProcessFrameInternal(const std::shared_ptr<MediaFrame> &media_frame) = 0;
	virtual FilterResult PopCompletedFrameInternal() = 0;

	int32_t GetInputWidth() const
	{
		return _src_width;
	}

	int32_t GetInputHeight() const
	{
		return _src_height;
	}

	// If the input track and output track are the same, the filter is used for a single track.
	// The main goal of this filter is to handle frame drops.
	bool IsSingleTrack() const
	{
		return (_input_track == _output_track) ? true : false;
	}

	void SetState(State state)
	{
		_state = state;
	}

	State GetState() const
	{
		return _state;
	}

	void SetInputStreamInfo(std::shared_ptr<info::Stream> input_stream_info)
	{
		_input_stream_info = input_stream_info;
	}

	void SetInputTrack(std::shared_ptr<MediaTrack> input_track)
	{
		_input_track = input_track;
	}

	void SetOutputStreamInfo(std::shared_ptr<info::Stream> output_stream_info)
	{
		_output_stream_info = output_stream_info;
	}

	void SetOutputTrack(std::shared_ptr<MediaTrack> output_track)
	{
		_output_track = output_track;
	}

	void SetDescription(const ov::String &description)
	{
		_description = description;
	}

	ov::String GetDescription() const
	{
		return _description;
	}

	void SetSourceId(int32_t source_id)
	{
		_source_id = source_id;
	}

	std::shared_ptr<info::Stream> GetInputStreamInfo() const
	{
		return _input_stream_info;
	}

	std::shared_ptr<info::Stream> GetOutputStreamInfo() const
	{
		return _output_stream_info;
	}

	std::shared_ptr<MediaTrack> GetInputTrack() const
	{
		return _input_track;
	}

	std::shared_ptr<MediaTrack> GetOutputTrack() const
	{
		return _output_track;
	}

	ov::String GetLogPrefix() const
	{
		if (_output_stream_info)
		{
			auto prefix = ov::String::FormatString("%s", _output_stream_info->GetUri().CStr());

			if (_output_track)
			{
				prefix += ov::String::FormatString("|T=%d,M=%s:%d", _output_track->GetId(), cmn::GetCodecModuleIdString(_output_track->GetCodecModuleId()), _output_track->GetCodecDeviceId());
			}
			return prefix;
		}

		return "";
	}

protected:
	virtual bool SendFrame(std::shared_ptr<MediaFrame> media_frame) = 0;
	virtual std::shared_ptr<MediaFrame> ReceiveFrame() = 0;

	std::atomic<State> _state = State::CREATED;

	cmn::VideoPixelFormatId _src_pixfmt = cmn::VideoPixelFormatId::None;
	int32_t 	_src_width = 0;
	int32_t 	_src_height = 0;

	ov::String 	_src_args = "";

	ov::String 	_filter_desc = "";
	ov::String 	_description = "";

	std::shared_ptr<info::Stream> _input_stream_info;
	std::shared_ptr<MediaTrack> _input_track;

	std::shared_ptr<info::Stream> _output_stream_info;
	std::shared_ptr<MediaTrack> _output_track;

	bool _use_hwframe_transfer = false;

	int32_t _source_id = 0;

	// Output frames drained from the backend pipeline, served by PopCompletedFrameInternal().
	std::queue<std::shared_ptr<MediaFrame>> _output_frames;
};
