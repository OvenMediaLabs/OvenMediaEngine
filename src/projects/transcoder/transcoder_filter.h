#pragma once

#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>

#include <stdint.h>

#include "base/info/stream.h"
#include "filter/filter_base.h"
#include "transcoder_context.h"

enum class TranscodeFilterType : int8_t
{
	None = -1,
	AudioResampler,
	VideoRescaler,

	Count  ///< Number of sample formats. DO NOT USE if linking dynamically
};

class TranscodeFilter
{
public:
	typedef std::function<void(TranscodeResult, int32_t, std::shared_ptr<MediaFrame>)> CompleteHandler;

	static std::shared_ptr<TranscodeFilter> Create(
		int32_t filter_id,
		const std::shared_ptr<info::Stream> &input_stream_info, std::shared_ptr<MediaTrack> input_track,
		const std::shared_ptr<info::Stream> &output_stream_info, std::shared_ptr<MediaTrack> output_track,
		CompleteHandler complete_handler);

	static std::shared_ptr<TranscodeFilter> Create(
		int32_t filter_id,
		const std::shared_ptr<info::Stream> &output_tsream_info, std::shared_ptr<MediaTrack> output_track,
		CompleteHandler complete_handler);

public:
	TranscodeFilter();
	~TranscodeFilter();
	
	bool Configure(
		int32_t filter_id,
		const std::shared_ptr<info::Stream> &input_stream_info, std::shared_ptr<MediaTrack> input_track,
		const std::shared_ptr<info::Stream> &output_stream_info, std::shared_ptr<MediaTrack> output_track);
	bool SendBuffer(std::shared_ptr<MediaFrame> buffer);
	void Stop(); 
	void Flush();
	
	cmn::Timebase GetInputTimebase() const;
	cmn::Timebase GetOutputTimebase() const;
	std::shared_ptr<MediaTrack> &GetInputTrack();
	std::shared_ptr<MediaTrack> &GetOutputTrack(); 

	void SetCompleteHandler(CompleteHandler complete_handler);
	void OnComplete(TranscodeResult result, std::shared_ptr<MediaFrame> frame);

	ov::String GetDescription() const;
	
private:
	bool CreateInternal();
	bool IsNeedUpdate(std::shared_ptr<MediaFrame> buffer);

	int32_t _id;

	int64_t _last_timestamp = -1LL;
	int64_t _timestamp_jump_threshold = 0LL;

	std::shared_ptr<info::Stream> _input_stream_info;
	std::shared_ptr<MediaTrack> _input_track;

	std::shared_ptr<info::Stream> _output_stream_info;
	std::shared_ptr<MediaTrack> _output_track;

	CompleteHandler _complete_handler;

	std::shared_mutex _mutex;
	std::shared_ptr<FilterBase> _internal;
};
