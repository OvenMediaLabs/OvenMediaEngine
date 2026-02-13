#include "transcoder_filter.h"

#include "filter/filter_resampler.h"
#include "filter/filter_rescaler.h"
#include "transcoder_gpu.h"
#include "transcoder_fault_injector.h"
#include "transcoder_private.h"

using namespace cmn;

#define PTS_INCREMENT_LIMIT 15
#define MAX_QUEUE_SIZE 5
#define ENABLE_QUEUE_EXCEED_WAIT true

TranscodeFilter::TranscodeFilter()
	: _internal(nullptr)
{
}

TranscodeFilter::~TranscodeFilter()
{
}

std::shared_ptr<TranscodeFilter> TranscodeFilter::Create(int32_t id,
														 const std::shared_ptr<info::Stream>& input_stream_info, std::shared_ptr<MediaTrack> input_track,
														 const std::shared_ptr<info::Stream>& output_stream_info, std::shared_ptr<MediaTrack> output_track,
														 CompleteHandler complete_handler)
{
	auto filter = std::make_shared<TranscodeFilter>();
	if (filter->Configure(id, input_stream_info, input_track, output_stream_info, output_track) == false)
	{
		return nullptr;
	}
	filter->SetCompleteHandler(complete_handler);
	return filter;
}

std::shared_ptr<TranscodeFilter> TranscodeFilter::Create(int32_t id,
														 const std::shared_ptr<info::Stream>& output_stream_info, std::shared_ptr<MediaTrack> output_track,
														 CompleteHandler complete_handler)
{
	auto filter = std::make_shared<TranscodeFilter>();
	if (filter->Configure(id, output_stream_info, output_track, output_stream_info, output_track) == false)
	{
		return nullptr;
	}
	filter->SetCompleteHandler(complete_handler);
	return filter;
}

bool TranscodeFilter::Configure(int32_t id,
								const std::shared_ptr<info::Stream>& input_stream_info, std::shared_ptr<MediaTrack> input_track,
								const std::shared_ptr<info::Stream>& output_stream_info, std::shared_ptr<MediaTrack> output_track)
{
	_id = id;
	_input_stream_info = input_stream_info;
	_input_track = input_track;

	_output_stream_info = output_stream_info;
	_output_track = output_track;
	
	_timestamp_jump_threshold = (int64_t)GetInputTrack()->GetTimeBase().GetTimescale() * PTS_INCREMENT_LIMIT;

	return CreateInternal();
}

bool TranscodeFilter::CreateInternal()
{
	std::lock_guard<std::shared_mutex> lock(_mutex);

	// If there is a previously created filter, remove it.
	if (_internal != nullptr)
	{
		_internal->Stop();
		_internal.reset();
		_internal = nullptr;
	}

	switch (GetInputTrack()->GetMediaType())
	{
		case MediaType::Audio:
			_internal = std::make_shared<FilterResampler>();
			break;
		case MediaType::Video:
			_internal = std::make_shared<FilterRescaler>();
			break;
		default:
			logte("Unsupported media type in filter");
			return false;
	}

	auto name = ov::String::FormatString("filter_%s", cmn::GetMediaTypeString(GetInputTrack()->GetMediaType()));
	auto urn = std::make_shared<info::ManagedQueue::URN>(
		_input_stream_info->GetApplicationName(),
		_input_stream_info->GetName(),
		"trs",
		name.LowerCaseString());
	_internal->SetQueueUrn(urn);
	_internal->SetQueuePolicy(ENABLE_QUEUE_EXCEED_WAIT, MAX_QUEUE_SIZE);
	_internal->SetCompleteHandler(bind(&TranscodeFilter::OnComplete, this, std::placeholders::_1, std::placeholders::_2));
	_internal->SetInputTrack(GetInputTrack());
	_internal->SetOutputTrack(GetOutputTrack());

	// Fault Injection for testing
	if (TranscodeFaultInjector::GetInstance()->IsEnabled() && (_input_stream_info != _output_stream_info))
	{
		if (TranscodeFaultInjector::GetInstance()->IsTriggered(
				TranscodeFaultInjector::ComponentType::FilterComponent,
				TranscodeFaultInjector::IssueType::InitFailed,
				GetOutputTrack()->GetCodecModuleId(),
				GetOutputTrack()->GetCodecDeviceId()) == true)
		{
			return false;
		}
	}

	return _internal->Start();
}

void TranscodeFilter::Stop()
{
	std::lock_guard<std::shared_mutex> lock(_mutex);

	if (_internal != nullptr)
	{
		_internal->Stop();
		_internal.reset();
		_internal = nullptr;
	}
}

void TranscodeFilter::Flush()
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	if (_internal != nullptr)
	{
		// Not implemented
	}
}

bool TranscodeFilter::SendBuffer(std::shared_ptr<MediaFrame> buffer)
{
	if (IsNeedUpdate(buffer) == true)
	{
		if (CreateInternal() == false)
		{
			logte("Failed to regenerate filter");
			return false;
		}

		return true;
	}

	std::shared_lock<std::shared_mutex> lock(_mutex);
	if (_internal == nullptr)
	{
		return false;
	}

	return _internal->SendBuffer(std::move(buffer));
}

bool TranscodeFilter::IsNeedUpdate(std::shared_ptr<MediaFrame> buffer)
{
	// Single track(paired with encoder) does not need to be updated.
	if (_internal == nullptr || _internal->IsSingleTrack() == true)
	{
		return false;
	}

	// Get the last timestamp and update it to the current timestamp.
	// This is used to detect abnormal timestamp changes.
	int64_t last_timestamp = _last_timestamp;
	int64_t curr_timestamp = buffer->GetPts();
	_last_timestamp = curr_timestamp;

	// Check #1 - Abnormal timestamp
	int64_t diff_timestamp = abs(curr_timestamp - last_timestamp);
	bool is_abnormal	   = (last_timestamp != -1LL && diff_timestamp > _timestamp_jump_threshold) ? true : false;
	if (is_abnormal)
	{
		logtw("[%s(%u)] Timestamp changed unexpectedly for track %u. last:%lld, curr:%lld, diff:%lld, threshold:%lld",
			  _input_stream_info->GetUri().CStr(), _input_stream_info->GetId(),
			  GetInputTrack()->GetId(), last_timestamp, curr_timestamp, diff_timestamp, _timestamp_jump_threshold);

		return true;
	}

	// Check #2 - Resolution change
	std::shared_lock<std::shared_mutex> lock(_mutex);

	if (GetInputTrack()->GetMediaType() == MediaType::Video)
	{
		if (buffer->GetWidth() != (int32_t)_internal->GetInputWidth() ||
			buffer->GetHeight() != (int32_t)_internal->GetInputHeight())
		{
			logtw("[%s(%u)] Resolution changed for track %u. (%dx%d -> %dx%d)",
				  _input_stream_info->GetUri().CStr(), _input_stream_info->GetId(),
				  GetInputTrack()->GetId(), _internal->GetInputWidth(), _internal->GetInputHeight(), buffer->GetWidth(), buffer->GetHeight());

			GetInputTrack()->SetWidth(buffer->GetWidth());
			GetInputTrack()->SetHeight(buffer->GetHeight());

			return true;
		}
	}

	// Check #3 - Filter error state
	//  When using an XMA scaler, resource allocation failures may occur intermittently.
	//  Avoid problems in this way until the underlying problem is resolved.
	if (_internal->GetState() == FilterBase::State::ERROR &&
		GetInputTrack()->GetCodecModuleId() == cmn::MediaCodecModuleId::XMA &&
		GetOutputTrack()->GetCodecModuleId() == cmn::MediaCodecModuleId::XMA)
	{
		logtw("[%s(%u)] It is assumed that the XMA resource allocation failed. So, recreate the filter.",
			  _input_stream_info->GetUri().CStr(), _input_stream_info->GetId());

		return true;
	}

	return false;
}

void TranscodeFilter::SetCompleteHandler(CompleteHandler complete_handler)
{
	_complete_handler = std::move(complete_handler);
}

void TranscodeFilter::OnComplete(TranscodeResult result, std::shared_ptr<MediaFrame> frame)
{

	// Fault Injection for testing
	if (TranscodeFaultInjector::GetInstance()->IsEnabled())
	{
		if (TranscodeFaultInjector::GetInstance()->IsTriggered(
				TranscodeFaultInjector::ComponentType::FilterComponent,
				TranscodeFaultInjector::IssueType::ProcessFailed,
				GetOutputTrack()->GetCodecModuleId(),
				GetOutputTrack()->GetCodecDeviceId()) == true)
		{
			result = TranscodeResult::DataError;
			frame  = nullptr;
		}

		if (TranscodeFaultInjector::GetInstance()->IsTriggered(
				TranscodeFaultInjector::ComponentType::FilterComponent,
				TranscodeFaultInjector::IssueType::Lagging,
				GetOutputTrack()->GetCodecModuleId(),
				GetOutputTrack()->GetCodecDeviceId()) == true)
		{
			usleep(300 * 1000);	 // 300ms
		}
	}

	if (!_complete_handler)
	{
		return;
	}

	// Set the codec module and device ID of the output track.
	// This is used when encoding with hardware acceleration.
	if (frame)
	{
		frame->SetCodecModuleId(GetOutputTrack()->GetCodecModuleId());
		frame->SetCodecDeviceId(GetOutputTrack()->GetCodecDeviceId());
	}

	_complete_handler(result, _id, frame);
}

cmn::Timebase TranscodeFilter::GetInputTimebase() const
{
	return _internal->GetInputTimebase();
}

cmn::Timebase TranscodeFilter::GetOutputTimebase() const
{
	return _internal->GetOutputTimebase();
}

std::shared_ptr<MediaTrack>& TranscodeFilter::GetInputTrack()
{
	return _input_track;
}

std::shared_ptr<MediaTrack>& TranscodeFilter::GetOutputTrack()
{
	return _output_track;
}

ov::String TranscodeFilter::GetDescription() const
{
	if(_internal == nullptr)
	{
		return "Null";
	}

	return _internal->GetDescription();
}