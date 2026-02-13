//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/application.h>
#include <stdint.h>

#include <memory>
#include <queue>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_type.h"
#include "transcoder_context.h"
#include "codec/codec_base.h"

class TranscoderAlerts
{
public:
	enum ErrorType : int8_t
	{
		CREATION_ERROR_PROFILE = 0,
		CREATION_ERROR_DECODER,
		CREATION_ERROR_FILTER,
		CREATION_ERROR_ENCODER,
		DECODING_ERROR,
		FILTERING_ERROR,
		ENCODING_ERROR
	};

	const uint32_t _error_evaluation_interval_ms = 5000; // 5 seconds

public:
	TranscoderAlerts();
	~TranscoderAlerts();

	void UpdateErrorCountIfNeeded(
		ErrorType error_type,
		std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track,
		std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track);

	void UpdateErrorWithoutCount(
		ErrorType error_type,
		std::shared_ptr<cfg::vhost::app::oprf::OutputProfile> output_profile,
		std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track,
		std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track);		
	
private:

	class ErrorRecord
	{
	public:
		static std::shared_ptr<ErrorRecord> Create(ErrorType type, uint32_t track_id)
		{
			auto record				 = std::make_shared<ErrorRecord>();
			record->error_type		 = type;
			record->track_id		 = track_id;
			record->first_error_time = std::chrono::system_clock::now();
			record->error_count		 = 1;
			return record;
		}

		uint32_t GetElapsedMs()
		{
			auto now = std::chrono::system_clock::now();
			return std::chrono::duration_cast<std::chrono::milliseconds>(now - first_error_time).count();
		}

		void Reset()
		{
			first_error_time = std::chrono::system_clock::now();
			error_count	  = 0;
		}

		void IncrementCount()
		{
			error_count++;
		}

		int32_t GetErrorCount()
		{
			return error_count;
		}
		
	public:
		std::chrono::system_clock::time_point first_error_time;
		int32_t error_count = 0;
		ErrorType error_type;
		uint32_t track_id;
	};

	// Mutex for thread safety can be added if needed
	std::shared_mutex _mutex;
	std::map<std::pair<ErrorType, uint32_t>, std::shared_ptr<ErrorRecord>> _error_records;
};