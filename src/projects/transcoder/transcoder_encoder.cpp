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

#include "codec/encoder/encoder_aac.h"
#include "codec/encoder/encoder_avc_nilogan.h"
#include "codec/encoder/encoder_avc_nv.h"
#include "codec/encoder/encoder_avc_openh264.h"
#include "codec/encoder/encoder_avc_qsv.h"
#include "codec/encoder/encoder_avc_x264.h"
#include "codec/encoder/encoder_avc_xma.h"
#include "codec/encoder/encoder_ffopus.h"
#include "codec/encoder/encoder_hevc_nilogan.h"
#include "codec/encoder/encoder_hevc_nv.h"
#include "codec/encoder/encoder_hevc_qsv.h"
#include "codec/encoder/encoder_hevc_xma.h"
#include "codec/encoder/encoder_jpeg.h"
#include "codec/encoder/encoder_opus.h"
#include "codec/encoder/encoder_png.h"
#include "codec/encoder/encoder_vp8.h"
#include "codec/encoder/encoder_webp.h"
#include "codec/encoder/encoder_whisper.h"
#include "transcoder_fault_injector.h"
#include "transcoder_gpu.h"
#include "transcoder_modules.h"
#include "transcoder_private.h"

#define USE_LEGACY_LIBOPUS false
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

	// If the track is not video, the default module is the only candidate.
	if (cmn::IsVideoCodec(track->GetCodecId()) == false)
	{
		candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), cmn::MediaCodecModuleId::DEFAULT, 0));
		return candidate_modules;
	}

	if(hwaccels_enable == true)
	{
		if(track->GetCodecModules().Trim().IsEmpty() == false)
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

	// ex) hwaccels_modules = "XMA:0,NV:0,QSV:0"
	std::vector<ov::String> desire_modules = configuration.Split(",");

	// If no modules are configured, all modules are designated as candidates.
	if (desire_modules.size() == 0 || configuration.IsEmpty() == true)
	{
		desire_modules.clear();
		
		if (hwaccels_enable == true)
		{
			desire_modules.push_back(ov::String::FormatString("%s:%d", "XMA", ALL_GPU_ID));
			desire_modules.push_back(ov::String::FormatString("%s:%d", "NV", ALL_GPU_ID));
			desire_modules.push_back(ov::String::FormatString("%s:%d", "QSV", ALL_GPU_ID));
			desire_modules.push_back(ov::String::FormatString("%s:%d", "NILOGAN", ALL_GPU_ID));
		}

		desire_modules.push_back(ov::String::FormatString("%s:%d", DEFAULT_MODULE_NAME, ALL_GPU_ID));
	}

	for(auto &desire_module : desire_modules)
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

		if(cmn::IsSupportHWAccels(module_id) == true && hwaccels_enable == false)
		{
			logtw("HWAccels is not enabled. Ignore this codec. module(%s)", module_name.CStr());
			continue;
		}
		else  if(cmn::IsSupportHWAccels(module_id) == true && hwaccels_enable == true)
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
			  candidate->GetCodecId(),
			  cmn::GetCodecModuleIdString(candidate->GetModuleId()),
			  candidate->GetModuleId(),
			  candidate->GetDeviceId());
	}

	return candidate_modules;
}

#define CASE_CREATE_CODEC_IFNEED(MODULE_ID, CLS)                                 \
	case cmn::MediaCodecModuleId::MODULE_ID:                                     \
		encoder = std::make_shared<CLS>(*info);                                  \
		if (encoder == nullptr)                                                  \
		{                                                                        \
			break;                                                               \
		}                                                                        \
		encoder->SetDeviceID(candidate->GetDeviceId());                          \
		encoder->SetEncoderId(encoder_id);                                       \
		encoder->SetCompleteHandler(complete_handler);                           \
		track->SetCodecModuleId(encoder->GetModuleID());                         \
		track->SetCodecDeviceId(encoder->GetDeviceID());                         \
		track->SetOriginBitstream(encoder->GetBitstreamFormat());                \
		if (encoder->Configure(track) == true)                                   \
		{                                                                        \
			if (TranscodeFaultInjector::GetInstance()->IsEnabled())              \
			{                                                                    \
				if (TranscodeFaultInjector::GetInstance()->IsTriggered(          \
						TranscodeFaultInjector::ComponentType::EncoderComponent, \
						TranscodeFaultInjector::IssueType::InitFailed,           \
						encoder->GetModuleID(), encoder->GetDeviceID()) == true) \
				{                                                                \
					encoder->Stop();                                             \
					encoder = nullptr;                                           \
					break;                                                       \
				}                                                                \
			}                                                                    \
			goto done;                                                           \
		}                                                                        \
		if (encoder != nullptr)                                                  \
		{                                                                        \
			encoder->Stop();                                                     \
			encoder = nullptr;                                                   \
		}                                                                        \
		break;


std::shared_ptr<TranscodeEncoder> TranscodeEncoder::Create(
	int32_t encoder_id,
	std::shared_ptr<info::Stream> info,
	std::shared_ptr<MediaTrack> track,
	std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidates,
	CompleteHandler complete_handler)
{
	std::shared_ptr<TranscodeEncoder> encoder = nullptr;
	std::shared_ptr<info::CodecCandidate> cur_candidate = nullptr;

	for (auto &candidate : *candidates)
	{
		cur_candidate = candidate;

		if (candidate->GetCodecId() == cmn::MediaCodecId::H264)
		{
			switch (candidate->GetModuleId())
			{
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderAVCxOpenH264);
				CASE_CREATE_CODEC_IFNEED(OPENH264, EncoderAVCxOpenH264);
				CASE_CREATE_CODEC_IFNEED(X264, EncoderAVCx264);
				CASE_CREATE_CODEC_IFNEED(QSV, EncoderAVCxQSV);
				CASE_CREATE_CODEC_IFNEED(NILOGAN, EncoderAVCxNILOGAN);
				CASE_CREATE_CODEC_IFNEED(XMA, EncoderAVCxXMA);
				CASE_CREATE_CODEC_IFNEED(NVENC, EncoderAVCxNV);
				default:
					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::H265)
		{
			switch (candidate->GetModuleId())
			{
				// No default module for HEVC
				CASE_CREATE_CODEC_IFNEED(QSV, EncoderHEVCxQSV);
				CASE_CREATE_CODEC_IFNEED(NILOGAN, EncoderHEVCxNILOGAN);
				CASE_CREATE_CODEC_IFNEED(XMA, EncoderHEVCxXMA);
				CASE_CREATE_CODEC_IFNEED(NVENC, EncoderHEVCxNV);
				default:
					break;
			};
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Vp8)
		{
			switch (candidate->GetModuleId())
			{
				default:
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderVP8);
				CASE_CREATE_CODEC_IFNEED(LIBVPX, EncoderVP8);

					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Aac)
		{
			switch (candidate->GetModuleId())
			{
				default:
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderAAC);
				CASE_CREATE_CODEC_IFNEED(FDKAAC, EncoderAAC);
					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Opus)
		{
			switch (candidate->GetModuleId())
			{
#if USE_LEGACY_LIBOPUS
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderOPUS);
#else
				default:
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderFFOPUS);
				CASE_CREATE_CODEC_IFNEED(LIBOPUS, EncoderFFOPUS);
#endif
					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Jpeg)
		{
			switch (candidate->GetModuleId())
			{
				default:
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderJPEG);
					break;
			}
			break;
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Png)
		{
			switch (candidate->GetModuleId())
			{
				default:
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderPNG);
					break;
			}
			break;
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Webp)
		{
			switch (candidate->GetModuleId())
			{
				default:
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderWEBP);
					break;
			}
			break;
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Whisper)
		{
			switch (candidate->GetModuleId())
			{
				default:
				CASE_CREATE_CODEC_IFNEED(DEFAULT, EncoderWhisper);
					break;
			}
		}
		else
		{
			OV_ASSERT(false, "Not supported codec: %d", track->GetCodecId());
		}

		// If the decoder is not created, try the next candidate.
		encoder = nullptr;
	}

done:
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
	  _bitstream_format(cmn::BitstreamFormat::Unknown),
	  _packet_type(cmn::PacketType::Unknown),
	  _kill_flag(false),
	  _complete_handler(nullptr),
	  _force_keyframe_by_time_interval(0),
	  _accumulate_frame_duration(-1),
	  _codec_context(nullptr),
	  _packet(nullptr),
	  _frame(nullptr)
{
}

TranscodeEncoder::~TranscodeEncoder()
{
	DeinitCodec();

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

	return (_track != nullptr);
}

std::shared_ptr<MediaTrack> &TranscodeEncoder::GetRefTrack()
{
	return _track;
}

void TranscodeEncoder::SendBuffer(std::shared_ptr<const MediaFrame> frame)
{
	// logte("%lld, msid:%u", frame->GetPts(), frame->GetMsid());
		
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
		logtt(ov::String::FormatString("encoder %s thread has ended", cmn::GetCodecIdString(GetCodecID())).CStr());
	}

	tc::TranscodeModules::GetInstance()->OnDeleted(true, GetCodecID(), GetModuleID(), GetDeviceID());
}

bool TranscodeEncoder::InitCodecInteral()
{
	_packet = ::av_packet_alloc();
	_frame = ::av_frame_alloc();

	// Called the codec specific initialization function.
	return InitCodec();
}

void TranscodeEncoder::DeinitCodec()
{
	if (_codec_context != nullptr)
	{
		if (_codec_context->codec != nullptr && _codec_context->codec->capabilities & AV_CODEC_CAP_ENCODER_FLUSH)
		{
			::avcodec_flush_buffers(_codec_context);
		}

		OV_SAFE_FUNC(_codec_context, nullptr, ::avcodec_free_context, &);
	}

	OV_SAFE_FUNC(_frame, nullptr, ::av_frame_free, &);
	OV_SAFE_FUNC(_packet, nullptr, ::av_packet_free, &);
}

void TranscodeEncoder::Flush()
{
	// Not implemented
}

bool TranscodeEncoder::PushProcess(std::shared_ptr<const MediaFrame> media_frame)
{
	// Flush the encoder if the frame is nullptr.
	if (media_frame == nullptr)
	{
		int ret = ::avcodec_send_frame(_codec_context, nullptr);
		if (ret < 0)
		{
			logte("Error sending a frame for encoding : %d", ret);
			
			return false;
		}

		return true;
	}

	// Convert MediaFrame to AVFrame
	auto av_frame = ffmpeg::compat::ToAVFrame(GetRefTrack()->GetMediaType(), media_frame);
	if (!av_frame)
	{
		logte("Could not allocate the video frame data");
		return false;
	}

	// Force inserts keyframes based on accumulated frame duration.
	if (GetRefTrack()->GetMediaType() == cmn::MediaType::Video)
	{
		av_frame->pict_type = AV_PICTURE_TYPE_NONE;

		// If _force_keyframe_by_time_interval value is > 0,
		// Enabled force keyframe by time interval.
		if (_force_keyframe_by_time_interval > 0)
		{
			if (_accumulate_frame_duration >= _force_keyframe_by_time_interval ||  // Accumulated duration exceeds keyframe interval
				_accumulate_frame_duration == -1)								   // Force keyframe the first frame
			{
				av_frame->pict_type		   = AV_PICTURE_TYPE_I;
				_accumulate_frame_duration = 0;
			}
			_accumulate_frame_duration += media_frame->GetDuration();
		}
	}

	// Send the frame to the encoder
	int ret = ::avcodec_send_frame(_codec_context, av_frame);
	if (ret == AVERROR(EAGAIN))
	{
		logtw("Encoder internal buffer is full, need to flush packets.");
	}
	else if (ret == AVERROR_INVALIDDATA)
	{
		logtw("Invalid data while sending a frame for encoding.");
	}
	else if (ret == AVERROR(ENOMEM))
	{
		logte("Could not allocate memory while sending a frame for encoding.");

		Complete(TranscodeResult::DataError, nullptr);

		return false;
	}
	else if (ret < 0)
	{
		logte("Error sending a frame for encoding. reason(%s)", ffmpeg::compat::AVErrorToString(ret).CStr());

		Complete(TranscodeResult::DataError, nullptr);

		return false;
	}

	return true;
}

// true: continue, false: stop
bool TranscodeEncoder::PopProcess()
{
	// Check frame is available
	int ret = ::avcodec_receive_packet(_codec_context, _packet);
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
	{
		// There is no more remain frame.
		return false;
	}
	else if (ret == AVERROR_INVALIDDATA)
	{
		logtw("Invalid data while receiving a packet for encoding.");

		return false;
	}
	else if (ret == AVERROR(ENOMEM))
	{
		logtw("Could not allocate memory while receiving a packet for encoding.");
		Complete(TranscodeResult::DataError, nullptr);

		return false;
	}
	else if (ret < 0)
	{
		logte("Error receiving a packet for encoding : %s", ffmpeg::compat::AVErrorToString(ret).CStr());

		Complete(TranscodeResult::DataError, nullptr);

		return false;
	}

	auto media_packet = ffmpeg::compat::ToMediaPacket(_packet, GetRefTrack()->GetMediaType(), _bitstream_format, _packet_type);
	::av_packet_unref(_packet);
	if (media_packet == nullptr)
	{
		logte("Could not allocate the media packet");

		Complete(TranscodeResult::DataError, nullptr);

		return false;
	}


	if (GetRefTrack()->GetMediaType() == cmn::MediaType::Audio)
	{
		// If the pts value are under zero, the dash packetizer does not work.
		if (media_packet->GetPts() < 0)
		{
			return true;
		}
	}
	
	// Call the complete handler.
	Complete(TranscodeResult::DataReady, std::move(media_packet));

	return true;
}

void TranscodeEncoder::CodecThread()
{
	ov::logger::ThreadHelper thread_helper;

	// Initialize the codec and notify to the main thread.
	if (_codec_init_event.Submit(InitCodecInteral()) == false)
	{
		return;
	}

	// Initialize for Force Keyframe by time interval.
	if ((GetRefTrack()->GetMediaType() == cmn::MediaType::Video) &&
		(GetRefTrack()->GetKeyFrameIntervalTypeByConfig() == cmn::KeyFrameIntervalType::TIME))
	{
		// Enable force keyframe by time interval.
		auto key_frame_interval_ms		 = GetRefTrack()->GetKeyFrameInterval();
		auto timebase_timescale			 = GetRefTrack()->GetTimeBase().GetTimescale();
		_force_keyframe_by_time_interval = static_cast<int64_t>(timebase_timescale * (double)key_frame_interval_ms / 1000.0);

		// Insert keyframe in first frame
		_accumulate_frame_duration		 = -1;

		if (key_frame_interval_ms > 0 && key_frame_interval_ms < 500) // 500ms 
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

	[[maybe_unused]] 
	int32_t curr_source_id = 0;

	while (!_kill_flag)
	{
		auto obj = _input_buffer.Dequeue();
		if (obj.has_value() == false)
			continue;

		auto media_frame = std::move(obj.value());

#ifdef HWACCELS_XMA_ENABLED
		///////////////////////////////////////////////////
		// Recreate the codec context if the source id is changed.
		// Xilinx VCU does not support frame buffer sharing between xvbm multi sclaler filter.
		///////////////////////////////////////////////////
		if (GetSupportVideoFormat() == cmn::VideoPixelFormatId::XVBM_8 || GetSupportVideoFormat() == cmn::VideoPixelFormatId::XVBM_10)
		{
			if (curr_source_id != media_frame->GetSourceId() && curr_source_id != 0)
			{
				///////////////////////////////////////////////////
				// Flush encoder
				///////////////////////////////////////////////////
				if (PushProcess(nullptr) == true)
				{
					while (PopProcess() == true && !_kill_flag)
					{
					}
				}

				///////////////////////////////////////////////////
				// Reinit codec
				///////////////////////////////////////////////////
				DeinitCodec();

				if (InitCodecInteral() == false)
				{
					break;
				}
			}
			curr_source_id = media_frame->GetSourceId();
		}
#endif

		///////////////////////////////////////////////////
		// Request frame encoding to codec
		///////////////////////////////////////////////////
		if(PushProcess(media_frame) == false)
		{
			break;
		}

		///////////////////////////////////////////////////
		// The encoded packet is taken from the codec.
		///////////////////////////////////////////////////
		while (PopProcess() == true && !_kill_flag)
		{
		}
	}
}

