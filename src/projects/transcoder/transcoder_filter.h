#pragma once

#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>
#include <modules/managed_queue/managed_queue.h>
#include <stdint.h>

#include "base/info/stream.h"
#include "filter/filter_base.h"
#include "media_frame.h"

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
		const std::shared_ptr<info::Stream> &output_stream_info, std::shared_ptr<MediaTrack> output_track,
		CompleteHandler complete_handler);
	bool SendBuffer(std::shared_ptr<MediaFrame> buffer);
	void Stop();
	void Flush() {} // Not implemented
	
	std::shared_ptr<info::Stream> GetInputStreamInfo() const;
	std::shared_ptr<info::Stream> GetOutputStreamInfo() const;
	std::shared_ptr<MediaTrack> GetInputTrack() const;
	std::shared_ptr<MediaTrack> GetOutputTrack() const;

	void SetCompleteHandler(CompleteHandler complete_handler);
	void OnComplete(TranscodeResult result, std::shared_ptr<MediaFrame> frame);

	ov::String GetDescription() const;

private:
	bool Initialize();
	std::shared_ptr<FilterBase> CreateBaseFilter();		
	std::shared_ptr<FilterBase> GetBaseFilter() const;

	void ThreadLoop();

	bool IsNeedUpdate(std::shared_ptr<MediaFrame> buffer);

	int32_t _id;

	int64_t _last_timestamp			  = -1LL;
	int64_t _timestamp_jump_threshold = 0LL;

	std::shared_ptr<info::Stream> _input_stream_info;
	std::shared_ptr<MediaTrack> _input_track;

	std::shared_ptr<info::Stream> _output_stream_info;
	std::shared_ptr<MediaTrack> _output_track;

	CompleteHandler _complete_handler;

	// Worker thread / queue / synchronization (owned by TranscodeFilter).
	std::atomic<bool> _kill_flag{false};
	std::thread _thread;
	ov::Future _codec_init_event;
	ov::ManagedQueue<std::shared_ptr<MediaFrame>> _input_buffer;

	std::atomic<bool> _setup_pending{false};

	mutable ov::SharedMutex _mutex;
	std::shared_ptr<FilterBase> _filter_base OV_GUARDED_BY(_mutex);
};
