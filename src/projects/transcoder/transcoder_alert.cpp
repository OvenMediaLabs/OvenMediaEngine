//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "transcoder_alert.h"

#include <monitoring/monitoring.h>

#include "transcoder_modules.h"
#include "transcoder_private.h"

TranscoderAlerts::TranscoderAlerts()
{
}

TranscoderAlerts::~TranscoderAlerts()
{
}

void TranscoderAlerts::UpdateErrorWithoutCount(
	ErrorType error_type,
	std::shared_ptr<cfg::vhost::app::oprf::OutputProfile> output_profile,
	std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track,
	std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track)
{
	switch (error_type)
	{
		case ErrorType::CREATION_ERROR_PROFILE: {
			if (input_stream == nullptr || output_profile == nullptr)
			{
				return;
			}

			auto extra_data = mon::alrt::ExtraData::Builder()
								  .OutputProfile(output_profile)
								  .Build();

			auto parent_stream_metric = StreamMetrics(*input_stream);

			MonitorInstance->GetAlert()->SendStreamMessage(
				mon::alrt::Message::Code::EGRESS_STREAM_CREATION_FAILED_OUTPUT_PROFILE,
				nullptr,
				parent_stream_metric,
				extra_data);
		}
		break;

		case ErrorType::CREATION_ERROR_DECODER: {
			if (input_stream == nullptr || input_track == nullptr)
			{
				return;
			}

			auto parent_stream_metric = StreamMetrics(*input_stream);
			auto codec_module		  = tc::TranscodeModules::GetInstance()->GetModule(false, input_track->GetCodecId(), input_track->GetCodecModuleId(), input_track->GetCodecDeviceId());
			auto extra_data			  = mon::alrt::ExtraData::Builder()
								  .CodecModule(codec_module)
								  .ParentSourceTrackId(input_track->GetId())
								  .Build();

			MonitorInstance->GetAlert()->SendStreamMessage(
				mon::alrt::Message::Code::EGRESS_STREAM_CREATION_FAILED_DECODER,
				nullptr,
				parent_stream_metric,
				extra_data);
		}
		break;

		case ErrorType::CREATION_ERROR_FILTER: {
			if (input_stream == nullptr || input_track == nullptr || output_stream == nullptr || output_track == nullptr || output_profile == nullptr)
			{
				return;
			}

			auto parent_stream_metric = StreamMetrics(*input_stream);
			auto stream_metric		  = StreamMetrics(*output_stream);
			auto codec_module		  = tc::TranscodeModules::GetInstance()->GetModule(true, output_track->GetCodecId(), output_track->GetCodecModuleId(), output_track->GetCodecDeviceId());
			auto extra_data			  = mon::alrt::ExtraData::Builder()
								  .CodecModule(codec_module)
								  .OutputProfile(output_profile)
								  .ParentSourceTrackId(input_track->GetId())
								  .SourceTrackId(output_track->GetId())
								  .Build();

			MonitorInstance->GetAlert()->SendStreamMessage(
				mon::alrt::Message::Code::EGRESS_STREAM_CREATION_FAILED_FILTER,
				stream_metric,
				parent_stream_metric,
				extra_data);
		}
		break;

		case ErrorType::CREATION_ERROR_ENCODER: {
			if (input_stream == nullptr || input_track == nullptr || output_stream == nullptr || output_track == nullptr || output_profile == nullptr)
			{
				return;
			}

			auto parent_stream_metric = StreamMetrics(*input_stream);
			auto stream_metric		  = StreamMetrics(*output_stream);
			auto codec_module		  = tc::TranscodeModules::GetInstance()->GetModule(true, output_track->GetCodecId(), output_track->GetCodecModuleId(), output_track->GetCodecDeviceId());
			auto extra_data			  = mon::alrt::ExtraData::Builder()
								  .CodecModule(codec_module)
								  .OutputProfile(output_profile)
								  .ParentSourceTrackId(input_track->GetId())
								  .SourceTrackId(output_track->GetId())
								  .Build();

			MonitorInstance->GetAlert()->SendStreamMessage(
				mon::alrt::Message::Code::EGRESS_STREAM_CREATION_FAILED_ENCODER,
				stream_metric,
				parent_stream_metric,
				extra_data);
		}
		break;
		default:
			return;
	}
}

void TranscoderAlerts::UpdateErrorCountIfNeeded(
	ErrorType error_type,
	std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track,
	std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track)
{
	uint32_t track_id;
	switch (error_type)
	{
		case ErrorType::DECODING_ERROR:
			track_id = input_track ? input_track->GetId() : 0;
			break;
		case ErrorType::ENCODING_ERROR:
			track_id = output_track ? output_track->GetId() : 0;
			break;
		case ErrorType::FILTERING_ERROR:
			track_id = output_track ? output_track->GetId() : 0;
			break;
		default:
			return;
	}

	// If there is no key, register a new one and send an alert
	// If there is a key and 5 seconds have not passed, increase the count and do not send an alert
	// If 5 seconds have passed, send an alert and reset the count
	bool is_needed_alert				= false;
	auto key							= std::make_pair(error_type, track_id);
	std::shared_ptr<ErrorRecord> record = nullptr;

	std::shared_lock<std::shared_mutex> read_lock(_mutex);
	auto record_it = _error_records.find(key);
	if (record_it == _error_records.end())
	{
		read_lock.unlock();

		// No existing record, create a new one and send alert
		record = ErrorRecord::Create(error_type, track_id);
		if (record)
		{
			std::unique_lock<std::shared_mutex> lock(_mutex);
			_error_records[key] = record;
			is_needed_alert		= true;
		}
	}
	else
	{
		read_lock.unlock();

		record = record_it->second;
		if (record->GetElapsedMs() <= _error_evaluation_interval_ms)
		{
			// Error evaluation window has not passed, increment count.
			record->IncrementCount();
			return;
		}
		else
		{
			// Reset record after sending alert
			is_needed_alert = true;
		}
	}

	// Send alert
	if (is_needed_alert == true)
	{
		logte("%s] Send transcoding errors to alert. errorType(%d), trackId(%u), elapse(%u), errorCount(%d)",
			  input_stream ? input_stream->GetUri().CStr() : output_stream ? output_stream->GetUri().CStr() : "UnknownStream",
			  error_type,
			  track_id,
			  record->GetElapsedMs(),
			  record->GetErrorCount());

		switch (error_type)
		{
			case ErrorType::DECODING_ERROR: {
				if (input_stream == nullptr || input_track == nullptr)
				{
					return;
				}

				auto parent_stream_metric = StreamMetrics(*input_stream);
				auto codec_module		  = tc::TranscodeModules::GetInstance()->GetModule(false, input_track->GetCodecId(), input_track->GetCodecModuleId(), input_track->GetCodecDeviceId());
				auto extra_data			  = mon::alrt::ExtraData::Builder()
									  .CodecModule(codec_module)
									  .ParentSourceTrackId(input_track->GetId())
									  .ErrorCount(record->GetErrorCount())
									  .ErrorEvaluationInterval(record->GetElapsedMs())
									  .Build();

				MonitorInstance->GetAlert()->SendStreamMessage(
					mon::alrt::Message::Code::EGRESS_TRANSCODE_FAILED_DECODING,
					nullptr,
					parent_stream_metric,
					extra_data);
			}
			break;

			case ErrorType::ENCODING_ERROR: {
				if (input_stream == nullptr || input_track == nullptr || output_stream == nullptr || output_track == nullptr)
				{
					return;
				}

				auto parent_stream_metric = StreamMetrics(*input_stream);
				auto stream_metric		  = StreamMetrics(*output_stream);
				auto codec_module		  = tc::TranscodeModules::GetInstance()->GetModule(true, output_track->GetCodecId(), output_track->GetCodecModuleId(), output_track->GetCodecDeviceId());
				auto extra_data			  = mon::alrt::ExtraData::Builder()
									  .CodecModule(codec_module)
									  .ParentSourceTrackId(input_track->GetId())
									  .SourceTrackId(output_track->GetId())
									  .ErrorCount(record->GetErrorCount())
									  .ErrorEvaluationInterval(record->GetElapsedMs())
									  .Build();

				MonitorInstance->GetAlert()->SendStreamMessage(
					mon::alrt::Message::Code::EGRESS_TRANSCODE_FAILED_ENCODING,
					stream_metric,
					parent_stream_metric,
					extra_data);
			}
			break;

			case ErrorType::FILTERING_ERROR: {
				if (input_stream == nullptr || input_track == nullptr || output_stream == nullptr || output_track == nullptr)
				{
					return;
				}

				auto parent_stream_metric = StreamMetrics(*input_stream);
				auto stream_metric		  = StreamMetrics(*output_stream);
				auto codec_module		  = tc::TranscodeModules::GetInstance()->GetModule(true, output_track->GetCodecId(), output_track->GetCodecModuleId(), output_track->GetCodecDeviceId());
				auto extra_data			  = mon::alrt::ExtraData::Builder()
									  .CodecModule(codec_module)
									  .ParentSourceTrackId(input_track->GetId())
									  .SourceTrackId(output_track->GetId())
									  .ErrorCount(record->GetErrorCount())
									  .ErrorEvaluationInterval(record->GetElapsedMs())
									  .Build();

				MonitorInstance->GetAlert()->SendStreamMessage(
					mon::alrt::Message::Code::EGRESS_TRANSCODE_FAILED_FILTERING,
					stream_metric,
					parent_stream_metric,
					extra_data);
			}
			break;
			default:
				return;
		}

		record->Reset();
	}
}