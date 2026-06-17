#include "transcoder_filter.h"

#include <base/ovlibrary/ovlibrary.h>

#include "filter/filter_lavfi_rescaler.h"
#include "filter/filter_lavfi_resampler.h"
#include "transcoder_gpu.h"
#include "transcoder_fault_injector.h"
#include "transcoder_private.h"


using namespace cmn;

#define PTS_INCREMENT_LIMIT 15
#define MAX_QUEUE_SIZE 5
#define ENABLE_QUEUE_EXCEED_WAIT true

TranscodeFilter::TranscodeFilter()
	: _filter_base(nullptr)
{
}

TranscodeFilter::~TranscodeFilter()
{
	Stop();
}

std::shared_ptr<TranscodeFilter> TranscodeFilter::Create(int32_t id,
														 const std::shared_ptr<info::Stream>& input_stream_info, std::shared_ptr<MediaTrack> input_track,
														 const std::shared_ptr<info::Stream>& output_stream_info, std::shared_ptr<MediaTrack> output_track,
														 CompleteHandler complete_handler)
{
	auto filter = std::make_shared<TranscodeFilter>();
	if (filter->Configure(id, input_stream_info, input_track, output_stream_info, output_track, complete_handler) == false)
	{
		return nullptr;
	}

	return filter;
} 

std::shared_ptr<TranscodeFilter> TranscodeFilter::Create(int32_t id,
														 const std::shared_ptr<info::Stream>& output_stream_info, std::shared_ptr<MediaTrack> output_track,
														 CompleteHandler complete_handler)
{
	auto filter = std::make_shared<TranscodeFilter>();
	if (filter->Configure(id, output_stream_info, output_track, output_stream_info, output_track, complete_handler) == false)
	{
		return nullptr;
	}

	return filter;
}

bool TranscodeFilter::Configure(int32_t id,
								const std::shared_ptr<info::Stream> &input_stream_info, std::shared_ptr<MediaTrack> input_track,
								const std::shared_ptr<info::Stream> &output_stream_info, std::shared_ptr<MediaTrack> output_track,
								CompleteHandler complete_handler)
{
	_id = id;

	_input_stream_info	= input_stream_info;
	_input_track		= input_track;
	_output_stream_info = output_stream_info;
	_output_track		= output_track;

	// The completion handler must be set before the worker thread starts producing frames.
	SetCompleteHandler(complete_handler);

	_timestamp_jump_threshold = (int64_t)GetInputTrack()->GetTimeBase().GetTimescale() * PTS_INCREMENT_LIMIT;

	// Initialize the input buffer queue.
	auto name = ov::String::FormatString("filter_%s", cmn::GetMediaTypeString(GetInputTrack()->GetMediaType()));
	auto urn  = std::make_shared<info::ManagedQueue::URN>(
		 GetInputStreamInfo()->GetApplicationName(),
		 GetInputStreamInfo()->GetName(),
		 "trs",
		 name.LowerCaseString());
	_input_buffer.SetUrn(urn);
	_input_buffer.SetThreshold(MAX_QUEUE_SIZE);
	_input_buffer.SetExceedWaitEnable(ENABLE_QUEUE_EXCEED_WAIT);

	try
	{
		_kill_flag = false;

		auto thread_name = ov::String::FormatString("FLT-%s-t%u", cmn::GetMediaTypeString(GetInputTrack()->GetMediaType()), GetOutputTrack()->GetId());
		_thread = std::thread(&TranscodeFilter::ThreadLoop, this);
		pthread_setname_np(_thread.native_handle(), thread_name.CStr());

		if (_codec_init_event.Get() == false)
		{
			_kill_flag = true;

			return false;
		}
	}
	catch (const std::system_error &e)
	{
		_kill_flag = true;

		logte("[%s] Failed to start filter thread", GetInputStreamInfo()->GetUri().CStr());

		return false;
	}

	return true;
}

std::shared_ptr<FilterBase> TranscodeFilter::CreateBaseFilter()
{
	std::shared_ptr<FilterBase> base = nullptr;

	switch (GetOutputTrack()->GetMediaType())
	{
		case MediaType::Audio:
			switch (GetOutputTrack()->GetCodecModuleId())
			{
				// Default to FFmpeg for SW codecs.
				case cmn::MediaCodecModuleId::DEFAULT:
				default:
					base = std::make_shared<FilterLavfiResampler>();
					break;
			}
			break;

		case MediaType::Video:
			switch (GetOutputTrack()->GetCodecModuleId())
			{
				// Default to FFmpeg for SW codecs.
				case cmn::MediaCodecModuleId::DEFAULT:
				default:
					base = std::make_shared<FilterLavfiRescaler>();
					break;
			}
			break;
		case MediaType::Subtitle:
				// TODO(Keukhan): The encoder's MediaType should be used instead of the output track's MediaType.
				// If the output track's MediaType is Subtitle but the encoder's MediaType is Audio, the filter should be created for Audio.
				base = std::make_shared<FilterLavfiResampler>();
				break;
		default:
			logte("Unsupported media type in filter. media_type:%s", cmn::GetMediaTypeString(GetOutputTrack()->GetMediaType()));
			return nullptr;
	}

	base->SetInputStreamInfo(GetInputStreamInfo());
	base->SetInputTrack(GetInputTrack());
	base->SetOutputStreamInfo(GetOutputStreamInfo());
	base->SetOutputTrack(GetOutputTrack());
	base->SetSourceId(ov::Random::GenerateInt32());

	// Fault Injection for testing
	if (TranscodeFaultInjector::GetInstance()->IsEnabled() && (GetInputStreamInfo() != GetOutputStreamInfo()))
	{
		if (TranscodeFaultInjector::GetInstance()->IsTriggered(
				TranscodeFaultInjector::ComponentType::FilterComponent,
				TranscodeFaultInjector::IssueType::InitFailed,
				GetOutputTrack()->GetCodecModuleId(),
				GetOutputTrack()->GetCodecDeviceId()) == true)
		{
			return nullptr;
		}
	}

	return base;
}

std::shared_ptr<FilterBase> TranscodeFilter::GetBaseFilter() const
{
	ov::SharedLockGuard lock(_mutex);

	return _filter_base;
}

bool TranscodeFilter::Initialize()
{
	auto base = CreateBaseFilter();
	if (base == nullptr)
	{
		return false;
	}

	if (base->Initialize() == false)
	{
		return false;
	}

	ov::LockGuard lock(_mutex);
	_filter_base = base;

	return true;
}


void TranscodeFilter::ThreadLoop()
{
	ov::logger::ThreadHelper thread_helper;

	if (_codec_init_event.Submit(Initialize()) == false)
	{
		return;
	}

	while (!_kill_flag)
	{
		auto obj = _input_buffer.Dequeue();
		if (obj.has_value() == false)
		{
			continue;
		}

		auto media_frame = std::move(obj.value());

		// Recreate the (Rescaler/Resampler) filter if needed.
		if (_setup_pending.exchange(false) == true)
		{
			if (Initialize() == false)
			{
				logte("[%s] Failed to reconfigure filter", _input_stream_info->GetUri().CStr());

				break;
			}
		}

		auto base = GetBaseFilter();
		if (base == nullptr)
		{
			continue;
		}

		// Feed the frame into the filter graph.
		auto sent = base->ProcessFrameInternal(media_frame);
		if (sent.result == TranscodeResult::DataError)
		{
			// NOTE: behavior preserved from the previous `fatal` flag, which was never
			// set to true — a filter error is reported downstream but does not stop the
			// thread. See FilterResult; flip to `break` here to make errors fatal.
			OnComplete(sent.result, std::move(sent.frame));
		}

		// Drain filtered frames.
		while (!_kill_flag)
		{
			auto recv = base->PopCompletedFrameInternal();
			if (recv.result == TranscodeResult::Again)
			{
				break;
			}

			OnComplete(recv.result, std::move(recv.frame));
		}
	}
}

void TranscodeFilter::Stop()
{
	_kill_flag = true;

	_input_buffer.Stop();

	if (_thread.joinable())
	{
		_thread.join();

		logtt("filter %s thread has ended", cmn::GetMediaTypeString(GetInputTrack()->GetMediaType()));
	}

	ov::LockGuard lock(_mutex);
	_filter_base.reset();
	_filter_base = nullptr;
}


bool TranscodeFilter::SendBuffer(std::shared_ptr<MediaFrame> buffer)
{
	if (IsNeedUpdate(buffer) == true)
	{
		logtd("[%s] Filter needs to be updated. reinitialize the filter. track:%u, pts:%" PRId64,
			  _input_stream_info->GetUri().CStr(), GetInputTrack()->GetId(), buffer->GetPts());

		_setup_pending = true;
	}

	_input_buffer.Enqueue(std::move(buffer));

	return true;
}

bool TranscodeFilter::IsNeedUpdate(std::shared_ptr<MediaFrame> buffer)
{
	ov::SharedLockGuard lock(_mutex);

	// Single track(paired with encoder) does not need to be updated.
	if (_filter_base == nullptr || _filter_base->IsSingleTrack() == true)
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
		logtw("[%s] Input timestamp has been changed unexpectedly. track:%u last:%" PRId64 ", curr:%" PRId64 ", diff:%" PRId64 ", threshold:%" PRId64,
			  _input_stream_info->GetUri().CStr(),
			  GetInputTrack()->GetId(),
			  last_timestamp,
			  curr_timestamp,
			  diff_timestamp,
			  _timestamp_jump_threshold);

		return true;
	}

	// Check #2 - Resolution changed. but, this is not warned because it can be a normal case.
	if (GetInputTrack()->GetMediaType() == MediaType::Video)
	{
		if (buffer->GetWidth() != (int32_t)_filter_base->GetInputWidth() ||
			buffer->GetHeight() != (int32_t)_filter_base->GetInputHeight())
		{
			logtd("[%s] input video frame resolution has been changed. track:%u. Size:%dx%d -> %dx%d",
				  _input_stream_info->GetUri().CStr(),
				  GetInputTrack()->GetId(),
				  _filter_base->GetInputWidth(),
				  _filter_base->GetInputHeight(),
				  buffer->GetWidth(),
				  buffer->GetHeight());

			GetInputTrack()->SetResolution(buffer->GetWidth(), buffer->GetHeight());

			return true;
		}
	}

	// Check #3 - Filter error state
	//  When using an XMA scaler, resource allocation failures may occur intermittently.
	//  Avoid problems in this way until the underlying problem is resolved.
	if (_filter_base->GetState() == FilterBase::State::ERROR &&
		GetInputTrack()->GetCodecModuleId() == cmn::MediaCodecModuleId::XMA &&
		GetOutputTrack()->GetCodecModuleId() == cmn::MediaCodecModuleId::XMA)
	{
		logtw("[%s] It is assumed that the XMA resource allocation failed. So, recreate the filter.",
			  _input_stream_info->GetUri().CStr());

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

	// Set the codec module and device ID of the output track.
	// This is used when encoding with hardware acceleration.
	if (frame)
	{
		frame->SetCodecModuleId(GetOutputTrack()->GetCodecModuleId());
		frame->SetCodecDeviceId(GetOutputTrack()->GetCodecDeviceId());
	}

	if (_complete_handler)
	{
		_complete_handler(result, _id, frame);
	}
}

std::shared_ptr<info::Stream> TranscodeFilter::GetInputStreamInfo() const
{
	return _input_stream_info;
}

std::shared_ptr<info::Stream> TranscodeFilter::GetOutputStreamInfo() const
{
	return _output_stream_info;
}

std::shared_ptr<MediaTrack> TranscodeFilter::GetInputTrack() const
{
	return _input_track;
}

std::shared_ptr<MediaTrack> TranscodeFilter::GetOutputTrack() const
{
	return _output_track;
}

ov::String TranscodeFilter::GetDescription() const
{
	ov::SharedLockGuard lock(_mutex);

	if (_filter_base == nullptr)
	{
		return "Null";
	}

	return _filter_base->GetDescription();
}
