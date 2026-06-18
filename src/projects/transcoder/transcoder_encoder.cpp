//==============================================================================
//
//  Transcoder
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "transcoder_encoder.h"

#include <utility>

#include "codec/encoder/encoder_avcodec_video.h"
#include "codec/encoder/encoder_avcodec_audio.h"
#include "codec/encoder/encoder_avcodec_image.h"
#include "codec/encoder/encoder_whisper.h"
#include "transcoder_fault_injector.h"
#include "transcoder_gpu.h"
#include "transcoder_modules.h"
#include "transcoder_private.h"



#define MAX_QUEUE_SIZE 2
#define ALL_GPU_ID -1
#define DEFAULT_MODULE_NAME "DEFAULT"


std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> TranscodeEncoder::GetCandidates(bool hwaccels_enable, ov::String hwaccles_modules, std::shared_ptr<MediaTrack> track)
{
	logtt("Track(%d) Codec(%s), HWAccels.Enable(%s), HWAccels.Modules(%s), Encode.Modules(%s)",
		  track->GetId(),
		  cmn::GetCodecIdString(track->GetCodecId()),
		  hwaccels_enable ? "true" : "false",
		  hwaccles_modules.CStr(),
		  track->GetCodecModules().CStr());

	ov::String configuration = ""; 
	std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidate_modules = std::make_shared<std::vector<std::shared_ptr<info::CodecCandidate>>>();

	// Non-video codecs with no explicit module config always use DEFAULT.
	// If a non-video codec (e.g. Whisper) sets <Modules>, it falls through to the normal hardware selection path.
	if (cmn::IsVideoCodec(track->GetCodecId()) == false && track->GetCodecModules().Trim().IsEmpty() == true)
	{
		candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), cmn::MediaCodecModuleId::DEFAULT, 0));
		return candidate_modules;
	}

	if (hwaccels_enable == true)
	{
		if (track->GetCodecModules().Trim().IsEmpty() == false)
		{
			configuration = track->GetCodecModules().Trim();
		}
		else
		{
			configuration = hwaccles_modules.Trim();
		}
	}
	else
	{
		configuration = track->GetCodecModules().Trim();
	}

	// ex) hwaccels_modules = "XMA:0,NV:0"
	std::vector<ov::String> desire_modules = configuration.Split(",");

	// If no modules are configured, all modules are designated as candidates.
	if (desire_modules.size() == 0 || configuration.IsEmpty() == true)
	{
		desire_modules.clear();
		
		if (hwaccels_enable == true)
		{
			desire_modules.push_back(ov::String::FormatString("%s:%d", "XMA", ALL_GPU_ID));
			desire_modules.push_back(ov::String::FormatString("%s:%d", "NV", ALL_GPU_ID));
			desire_modules.push_back(ov::String::FormatString("%s:%d", "NILOGAN", ALL_GPU_ID));
		}

		desire_modules.push_back(ov::String::FormatString("%s:%d", DEFAULT_MODULE_NAME, ALL_GPU_ID));
	}

	for (auto &desire_module : desire_modules)
	{
		// Pattern : <module_name>:<gpu_id> or <module_name>
		ov::Regex pattern_regex = ov::Regex::CompiledRegex( "(?<module_name>[^,:\\s]+[\\w]+):?(?<gpu_id>[^,]*)");

		auto matches = pattern_regex.Matches(desire_module.CStr());
		if (matches.GetError() != nullptr || matches.IsMatched() == false)
		{
			logtw("Incorrect pattern in the Modules item. module(%s)", desire_module.CStr());
			continue;;
		}

		auto named_group = matches.GetNamedGroupList();

		auto module_name 	= named_group["module_name"].GetValue();
		auto gpu_id 		= named_group["gpu_id"].GetValue().IsEmpty()?ALL_GPU_ID:ov::Converter::ToInt32(named_group["gpu_id"].GetValue());

		// Module Name(Codec Library Name) to Codec Library ID
		cmn::MediaCodecModuleId module_id = cmn::GetCodecModuleIdByName(module_name);
		if(module_id == cmn::MediaCodecModuleId::None)
		{
			logtw("Unknown codec module. name(%s)", module_name.CStr());
			continue;
		}

		if (cmn::IsSupportHWAccels(module_id) == true && hwaccels_enable == false)
		{
			logtw("HWAccels is not enabled. Ignore this codec. module(%s)", module_name.CStr());
			continue;
		}
		else  if (cmn::IsSupportHWAccels(module_id) == true && hwaccels_enable == true)
		{
			for (int device_id = 0; device_id < TranscodeGPU::GetInstance()->GetDeviceCount(module_id); device_id++)
			{
				if ((gpu_id == ALL_GPU_ID || gpu_id == device_id) && TranscodeGPU::GetInstance()->IsSupported(module_id, device_id) == true)
				{
					candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), module_id, device_id));
				}
			}
		}
		else
		{
			candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), module_id, 0));
		}
	}


	for (auto &candidate : *candidate_modules)
	{
		(void)(candidate);
		
		logtt("Candidate module: %s(%d), %s(%d):%d",
			  cmn::GetCodecIdString(candidate->GetCodecId()),
			  static_cast<int>(candidate->GetCodecId()),
			  cmn::GetCodecModuleIdString(candidate->GetModuleId()),
			  static_cast<int>(candidate->GetModuleId()),
			  candidate->GetDeviceId());
	}

	return candidate_modules;
}

std::shared_ptr<TranscodeEncoder> TranscodeEncoder::Instantiate(
	cmn::MediaCodecId codec_id,
	cmn::MediaCodecModuleId module_id,
	const info::Stream &stream_info)
{
	if (codec_id == cmn::MediaCodecId::H264)
	{
		switch (module_id)
		{
			case cmn::MediaCodecModuleId::DEFAULT:
			case cmn::MediaCodecModuleId::OPENH264: return std::make_shared<AVCodecVideoEncoder>(stream_info, codec_id, cmn::MediaCodecModuleId::OPENH264);
			case cmn::MediaCodecModuleId::X264:     return std::make_shared<AVCodecVideoEncoder>(stream_info, codec_id, cmn::MediaCodecModuleId::X264);
			default: break;
		}
	}
	else if (codec_id == cmn::MediaCodecId::H265)
	{
		switch (module_id)
		{
			default: break;
		}
	}
	else if (codec_id == cmn::MediaCodecId::Vp8)    return std::make_shared<AVCodecVideoEncoder>(stream_info, codec_id, cmn::MediaCodecModuleId::LIBVPX);
	else if (codec_id == cmn::MediaCodecId::Aac)    return std::make_shared<AVCodecAudioEncoder>(stream_info, codec_id);
	else if (codec_id == cmn::MediaCodecId::Opus)
	{
		return std::make_shared<AVCodecAudioEncoder>(stream_info, codec_id);
	}
	else if (codec_id == cmn::MediaCodecId::Jpeg)    return std::make_shared<AVCodecImageEncoder>(stream_info, codec_id);
	else if (codec_id == cmn::MediaCodecId::Png)     return std::make_shared<AVCodecImageEncoder>(stream_info, codec_id);
	else if (codec_id == cmn::MediaCodecId::Webp)    return std::make_shared<AVCodecImageEncoder>(stream_info, codec_id);
	else if (codec_id == cmn::MediaCodecId::Whisper) return std::make_shared<EncoderWhisper>(stream_info);
	else
	{
		OV_ASSERT(false, "Not supported codec: %d", static_cast<int>(codec_id));
	}

	return nullptr;
}

std::shared_ptr<TranscodeEncoder> TranscodeEncoder::Create(
	int32_t encoder_id,
	std::shared_ptr<info::Stream> info,
	std::shared_ptr<MediaTrack> track,
	std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidates,
	CompleteHandler complete_handler)
{
	std::shared_ptr<TranscodeEncoder> encoder = nullptr;

	for (auto &candidate : *candidates)
	{
		encoder = Instantiate(candidate->GetCodecId(), candidate->GetModuleId(), *info);
		if (encoder == nullptr)
		{
			continue;
		}

		encoder->SetDeviceID(candidate->GetDeviceId());
		encoder->SetEncoderId(encoder_id);
		encoder->SetCompleteHandler(complete_handler);
		track->SetCodecModuleId(encoder->GetModuleID());
		track->SetCodecDeviceId(encoder->GetDeviceID());
		track->SetOriginBitstream(encoder->GetBitstreamFormat());

		if (encoder->Configure(track) == true)
		{
			if (TranscodeFaultInjector::GetInstance()->IsEnabled())
			{
				if (TranscodeFaultInjector::GetInstance()->IsTriggered(
						TranscodeFaultInjector::ComponentType::EncoderComponent,
						TranscodeFaultInjector::IssueType::InitFailed,
						encoder->GetModuleID(), encoder->GetDeviceID()) == true)
				{
					encoder->Stop();
					encoder = nullptr;
					continue;
				}
			}
			break;
		}

		encoder->Stop();
		encoder = nullptr;
	}

	if (encoder)
	{
		logtt("The encoder has been created. track(#%d), codec(%s), module(%s:%d)",
			  track->GetId(),
			  cmn::GetCodecIdString(track->GetCodecId()),
			  cmn::GetCodecModuleIdString(track->GetCodecModuleId()),
			  track->GetCodecDeviceId());
	}

	return encoder;
}

TranscodeEncoder::TranscodeEncoder(info::Stream stream_info)
	: _encoder_id(-1),
	  _stream_info(stream_info),
	  _track(nullptr),
	  _kill_flag(false),
	  _complete_handler(nullptr),
	  _force_keyframe_by_time_interval(0),
	  _accumulate_frame_duration(-1)
{
}

TranscodeEncoder::~TranscodeEncoder()
{
	_input_buffer.Clear();
}


cmn::Timebase TranscodeEncoder::GetTimebase() const
{
	return _track->GetTimeBase();
}

void TranscodeEncoder::SetEncoderId(int32_t encoder_id)
{
	_encoder_id = encoder_id;
}

bool TranscodeEncoder::Configure(std::shared_ptr<MediaTrack> output_track)
{
	return Configure(output_track, MAX_QUEUE_SIZE);
}

bool TranscodeEncoder::Configure(std::shared_ptr<MediaTrack> output_track, size_t max_queue_size)
{
	_track = output_track;	

	auto name = ov::String::FormatString("enc_%s_t%d", cmn::GetCodecIdString(GetCodecID()), _track->GetId());
	auto urn = std::make_shared<info::ManagedQueue::URN>(_stream_info.GetApplicationInfo().GetVHostAppName(), _stream_info.GetName(), "trs", name);
	_input_buffer.SetUrn(urn);
	_input_buffer.SetThreshold(max_queue_size);

	// This is used to prevent the from creating frames from rescaler/resampler filter. 
	// Because of hardware resource limitations.
	_input_buffer.SetExceedWaitEnable(true);

	// SkipMessage is enabled due to the high possibility of queue overflow due to insufficient video encoding performance.
	// Users will not experience any inconvenience even if the video is intermittently missing.
	// However, it is sensitive when the audio cuts out.
	// if(_track->GetMediaType() == cmn::MediaType::Video)
	// {
	// 	_input_buffer.SetSkipMessageEnable(true);
	// }

	tc::TranscodeModules::GetInstance()->OnCreated(true, GetCodecID(), GetModuleID(), GetDeviceID());

	if (_track == nullptr)
	{
		return false;
	}

	// Start the encoding thread and wait for codec initialization to complete.
	try
	{
		_kill_flag = false;
		_codec_thread = std::thread([this]() { ThreadLoop(); });
		pthread_setname_np(_codec_thread.native_handle(), name.CStr());

		if (_codec_init_event.Get() == false)
		{
			_kill_flag = true;
			return false;
		}
	}
	catch (const std::system_error &e)
	{
		_kill_flag = true;
		return false;
	}

	return true;
}

std::shared_ptr<MediaTrack> &TranscodeEncoder::GetRefTrack()
{
	return _track;
}

void TranscodeEncoder::SendBuffer(std::shared_ptr<const MediaFrame> frame)
{
	// logte("%" PRId64 ", msid:%u", frame->GetPts(), frame->GetMsid());
		
	if (_input_buffer.IsExceedWaitEnable() == true)
	{
		_input_buffer.Enqueue(std::move(frame), false, 1000);
	}
	else
	{
		_input_buffer.Enqueue(std::move(frame));
	}
}

void TranscodeEncoder::SetCompleteHandler(CompleteHandler complete_handler)
{
	_complete_handler = std::move(complete_handler);
}

void TranscodeEncoder::Complete(TranscodeResult result, std::shared_ptr<MediaPacket> packet)
{
	// Fault Injection for testing
	if (TranscodeFaultInjector::GetInstance()->IsEnabled())
	{
		if (TranscodeFaultInjector::GetInstance()->IsTriggered(
				TranscodeFaultInjector::ComponentType::EncoderComponent,
				TranscodeFaultInjector::IssueType::ProcessFailed,
				GetModuleID(),
				GetDeviceID()) == true)
		{
			result = TranscodeResult::DataError;
			packet = nullptr;
		}

		if (TranscodeFaultInjector::GetInstance()->IsTriggered(
				TranscodeFaultInjector::ComponentType::EncoderComponent,
				TranscodeFaultInjector::IssueType::Lagging,
				GetModuleID(),
				GetDeviceID()) == true)
		{
			usleep(300 * 1000);	 // 300ms
		}
	}

	if (!_complete_handler)
	{
		return;
	}

	_complete_handler(result, _encoder_id, std::move(packet));
}

void TranscodeEncoder::Stop()
{
	_kill_flag = true;

	_input_buffer.Stop();

	if (_codec_thread.joinable())
	{
		_codec_thread.join();
		logtt("encoder %s thread has ended", cmn::GetCodecIdString(GetCodecID()));
	}

	tc::TranscodeModules::GetInstance()->OnDeleted(true, GetCodecID(), GetModuleID(), GetDeviceID());
}

void TranscodeEncoder::Flush()
{
	// Not implemented
}

bool TranscodeEncoder::Reinitialize()
{
	Uninitialize();

	return Initialize();
}

void TranscodeEncoder::SetupForceKeyframeByTime()
{
	if ((GetRefTrack()->GetMediaType() == cmn::MediaType::Video) &&
		(GetRefTrack()->GetKeyFrameIntervalTypeByConfig() == cmn::KeyFrameIntervalType::TIME))
	{
		// Enable force keyframe by time interval.
		auto key_frame_interval_ms		 = GetRefTrack()->GetKeyFrameInterval();
		auto timebase_timescale			 = GetRefTrack()->GetTimeBase().GetTimescale();
		_force_keyframe_by_time_interval = static_cast<int64_t>(timebase_timescale * (double)key_frame_interval_ms / 1000.0);

		// Insert keyframe in first frame
		_accumulate_frame_duration = -1;

		if (key_frame_interval_ms > 0 && key_frame_interval_ms < 500)  // 500ms
		{
			logtw("Force keyframe interval(by time) is enabled. but, interval is too short (%.0f ms). It may cause quality issues.", key_frame_interval_ms);
		}
		else
		{
			logti("Force keyframe interval(by time) is enabled. interval(%.0fms)", key_frame_interval_ms);
		}
	}
	else
	{
		// Disable force keyframe by time interval.
		_force_keyframe_by_time_interval = 0;
		_accumulate_frame_duration		 = -1;

		logtt("Force keyframe by time interval is disabled.");
	}
}

bool TranscodeEncoder::ComputeForceKeyframe(const std::shared_ptr<const MediaFrame> &frame)
{
	if (GetRefTrack()->GetMediaType() != cmn::MediaType::Video)
	{
		return false;
	}

	if (_force_keyframe_by_time_interval <= 0)
	{
		return false;
	}

	bool force = false;
	if (_accumulate_frame_duration >= _force_keyframe_by_time_interval ||  // Accumulated duration exceeds keyframe interval
		_accumulate_frame_duration == -1)								  // Force keyframe the first frame
	{
		_last_keyframe_delta_time  = _accumulate_frame_duration;
		force					   = true;
		_accumulate_frame_duration = 0;
	}
	_accumulate_frame_duration += frame->GetDuration();

	return force;
}

void TranscodeEncoder::ThreadLoop()
{
	ov::logger::ThreadHelper thread_helper;

	// Initialize the codec and notify the main thread.
	if (_codec_init_event.Submit(Initialize()) == false)
	{
		return;
	}

	// Initialize force-keyframe-by-time-interval state.
	SetupForceKeyframeByTime();

	while (!_kill_flag)
	{
		auto obj = _input_buffer.Dequeue();
		if (obj.has_value() == false)
		{
			continue;
		}

		auto media_frame = std::move(obj.value());

		// Reinitialize the codec if the current frame requires it (e.g. XVBM source change).
		if (NeedReinitForFrame(media_frame) == true)
		{
			// Flush and drain the encoder before reinitializing.
			auto sent = SendFrame(nullptr, false);
			if (sent.result == TranscodeResult::DataError)
			{
				break;
			}

			// Flush encoder and drain remaining packets. 
			while (!_kill_flag)
			{
				auto received = ReceivePacket();
				if ( received.result == TranscodeResult::Again)
				{
					break;
				}

				Complete(received.result, std::move(received.packet));
			}

			if (Reinitialize() == false)
			{
				break;
			}
		}

		// Send the frame to the encoder (force-keyframe decision is FFmpeg-free, made here).
		bool force_keyframe = ComputeForceKeyframe(media_frame);

		auto sent = SendFrame(media_frame, force_keyframe);
		if (sent.result == TranscodeResult::DataReady)
		{
			Complete(sent.result, std::move(sent.packet));
		}
		else if (sent.result == TranscodeResult::DataError)
		{
			logte("Error occurred while sending a frame for encoding. frame_pts(%" PRId64 "), reason(%s)", media_frame->GetPts(), sent.error.CStr());
			break;
		}
		// else if(sent.result == TranscodeResult::Again) {}


		// Drain encoded packets.
		while (!_kill_flag)
		{
			auto recv = ReceivePacket();
			if (recv.result == TranscodeResult::Again)
			{
				break;
			}
			else if (recv.result == TranscodeResult::DataReady)
			{
				Complete(recv.result, std::move(recv.packet));
			}
			else if (recv.result == TranscodeResult::DataError)
			{
				logte("Error occurred while receiving a packet for encoding. frame_pts(%" PRId64 "), reason(%s)", media_frame->GetPts(), recv.error.CStr());
				break;
			}
		}
	}
}
