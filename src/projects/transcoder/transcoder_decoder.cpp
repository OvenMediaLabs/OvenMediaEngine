//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "transcoder_decoder.h"

#include "codec/decoder/decoder_aac.h"
#include "codec/decoder/decoder_avc.h"
#include "codec/decoder/decoder_avc_nilogan.h"
#include "codec/decoder/decoder_avc_nv.h"
#include "codec/decoder/decoder_avc_qsv.h"
#include "codec/decoder/decoder_avc_xma.h"
#include "codec/decoder/decoder_hevc.h"
#include "codec/decoder/decoder_hevc_nilogan.h"
#include "codec/decoder/decoder_hevc_nv.h"
#include "codec/decoder/decoder_hevc_qsv.h"
#include "codec/decoder/decoder_hevc_xma.h"
#include "codec/decoder/decoder_mp3.h"
#include "codec/decoder/decoder_opus.h"
#include "codec/decoder/decoder_vp8.h"
#include "transcoder_gpu.h"
#include "transcoder_modules.h"
#include "transcoder_fault_injector.h"
#include "transcoder_private.h"

// Default is 300 (about 10 seconds for 30fps)
#define MAX_QUEUE_SIZE 30 * 10
#define ALL_GPU_ID -1
#define DEFAULT_MODULE_NAME "DEFAULT"

std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> TranscodeDecoder::GetCandidates(bool hwaccels_enable, ov::String hwaccles_modules, std::shared_ptr<MediaTrack> track)
{
	logtt("Codec(%s), HWAccels.Enable(%s), HWAccels.Modules(%s)",
		  cmn::GetCodecIdString(track->GetCodecId()), hwaccels_enable ? "true" : "false", hwaccles_modules.CStr());

	ov::String configuration = "";
	std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidate_modules = std::make_shared<std::vector<std::shared_ptr<info::CodecCandidate>>>();

	if (hwaccels_enable == true)
	{
		configuration = hwaccles_modules.Trim();
	}
	else
	{
		configuration = "";
	}

	// If the track is not video, the default module is the only candidate.
	if (cmn::IsVideoCodec(track->GetCodecId()) == false)
	{
		candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), cmn::MediaCodecModuleId::DEFAULT, 0));
		return candidate_modules;
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

	for (auto &desire_module : desire_modules)
	{
		// Pattern : <module_name>:<gpu_id> or <module_name>
		ov::Regex pattern_regex = ov::Regex::CompiledRegex("(?<module_name>[^,:\\s]+[\\w]+):?(?<gpu_id>[^,]*)");

		auto matches = pattern_regex.Matches(desire_module.CStr());
		if (matches.GetError() != nullptr || matches.IsMatched() == false)
		{
			logtw("Incorrect pattern in the Modules item. module(%s)", desire_module.CStr());

			continue;
			;
		}
		auto named_group = matches.GetNamedGroupList();

		auto module_name = named_group["module_name"].GetValue();
		auto gpu_id = named_group["gpu_id"].GetValue().IsEmpty() ? ALL_GPU_ID : ov::Converter::ToInt32(named_group["gpu_id"].GetValue());

		// If Unknown module name, skip.
		cmn::MediaCodecModuleId module_id = cmn::GetCodecModuleIdByName(module_name);
		if (module_id == cmn::MediaCodecModuleId::None)
		{
			logtw("Unknown codec module. name(%s)", module_name.CStr());
			continue;
		}

		// If hardware usage is enabled, check if the module is supported.
		if (hwaccels_enable == true)
		{
			for (int device_id = 0; device_id < TranscodeGPU::GetInstance()->GetDeviceCount(module_id); device_id++)
			{
				if ((gpu_id == ALL_GPU_ID || gpu_id == device_id) && TranscodeGPU::GetInstance()->IsSupported(module_id, device_id) == true)
				{
					candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), module_id, device_id));
				}
			}
		}

		//
		if (module_id == cmn::MediaCodecModuleId::DEFAULT)
		{
			candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), module_id, 0));
		}
	}

	for (auto &candidate : *candidate_modules)
	{
		(void)(candidate);

		logtt("Candidate module: %s(%u), %s(%u):%d",
			  cmn::GetCodecIdString(candidate->GetCodecId()),
			  ov::ToUnderlyingType(candidate->GetCodecId()),
			  cmn::GetCodecModuleIdString(candidate->GetModuleId()),
			  ov::ToUnderlyingType(candidate->GetModuleId()),
			  candidate->GetDeviceId());
	}

	return candidate_modules;
}

#define CASE_CREATE_CODEC_IFNEED(MODULE_ID, CLS)                                 \
	case cmn::MediaCodecModuleId::MODULE_ID:                                     \
		decoder = std::make_shared<CLS>(*info);                                  \
		if (decoder == nullptr)                                                  \
		{                                                                        \
			break;                                                               \
		}                                                                        \
		decoder->SetDeviceID(candidate->GetDeviceId());                          \
		decoder->SetDecoderId(decoder_id);                                       \
		decoder->SetCompleteHandler(complete_handler);                           \
		track->SetCodecModuleId(decoder->GetModuleID());                         \
		track->SetCodecDeviceId(decoder->GetDeviceID());                         \
		if (decoder->Configure(track) == true)                                   \
		{                                                                        \
			if (TranscodeFaultInjector::GetInstance()->IsEnabled())              \
			{                                                                    \
				if (TranscodeFaultInjector::GetInstance()->IsTriggered(          \
						TranscodeFaultInjector::ComponentType::DecoderComponent, \
						TranscodeFaultInjector::IssueType::InitFailed,           \
						decoder->GetModuleID(),                                  \
						decoder->GetDeviceID()) == true)                         \
				{                                                                \
					decoder->Stop();                                             \
					decoder = nullptr;                                           \
					break;                                                       \
				}                                                                \
			}                                                                    \
			goto done;                                                           \
		}                                                                        \
		if (decoder != nullptr)                                                  \
		{                                                                        \
			decoder->Stop();                                                     \
			decoder = nullptr;                                                   \
		}                                                                        \
		break;

std::shared_ptr<TranscodeDecoder> TranscodeDecoder::Create(
	int32_t decoder_id,
	std::shared_ptr<info::Stream> info,
	std::shared_ptr<MediaTrack> track,
	std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidates,
	CompleteHandler complete_handler)
{
	std::shared_ptr<TranscodeDecoder> decoder = nullptr;
	std::shared_ptr<info::CodecCandidate> cur_candidate = nullptr;

	for (auto &candidate : *candidates)
	{
		cur_candidate = candidate;

		if (candidate->GetCodecId() == cmn::MediaCodecId::H264)
		{
			switch (candidate->GetModuleId())
			{
				CASE_CREATE_CODEC_IFNEED(DEFAULT, DecoderAVC)
				CASE_CREATE_CODEC_IFNEED(QSV, DecoderAVCxQSV)
				CASE_CREATE_CODEC_IFNEED(NILOGAN, DecoderAVCxNILOGAN)
				CASE_CREATE_CODEC_IFNEED(NVENC, DecoderAVCxNV)
				CASE_CREATE_CODEC_IFNEED(XMA, DecoderAVCxXMA)
				default:
					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::H265)
		{
			switch (candidate->GetModuleId())
			{
				CASE_CREATE_CODEC_IFNEED(DEFAULT, DecoderHEVC)
				CASE_CREATE_CODEC_IFNEED(QSV, DecoderHEVCxQSV)
				CASE_CREATE_CODEC_IFNEED(NILOGAN, DecoderHEVCxNILOGAN)
				CASE_CREATE_CODEC_IFNEED(NVENC, DecoderHEVCxNV)
				CASE_CREATE_CODEC_IFNEED(XMA, DecoderHEVCxXMA)
				default:
					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Vp8)
		{
			switch (candidate->GetModuleId())
			{
				default:
					CASE_CREATE_CODEC_IFNEED(DEFAULT, DecoderVP8)

					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Aac)
		{
			switch (candidate->GetModuleId())
			{
				default:
					CASE_CREATE_CODEC_IFNEED(DEFAULT, DecoderAAC)
					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Opus)
		{
			switch (candidate->GetModuleId())
			{
				default:
					CASE_CREATE_CODEC_IFNEED(DEFAULT, DecoderOPUS)
					break;
			}
		}
		else if (candidate->GetCodecId() == cmn::MediaCodecId::Mp3)
		{
			switch (candidate->GetModuleId())
			{
				default:
					CASE_CREATE_CODEC_IFNEED(DEFAULT, DecoderMP3)
					break;
			}
		}
		else
		{
			OV_ASSERT(false, "Not supported codec: %d", track->GetCodecId());
		}

		// If the decoder is not created, try the next candidate.
		decoder = nullptr;
	}

done:
	if (decoder != nullptr)
	{

		logtt("The decoder has been created. track(#%d) codec(%s), module(%s:%d)",
			  track->GetId(),
			  cmn::GetCodecIdString(track->GetCodecId()),
			  cmn::GetCodecModuleIdString(track->GetCodecModuleId()),
			  track->GetCodecDeviceId());
	}

	return decoder;
}

TranscodeDecoder::TranscodeDecoder(info::Stream stream_info)
	: _decoder_id(-1),
	  _stream_info(stream_info),
	  _track(nullptr),
	  _complete_handler(nullptr),
	  _change_format(false),
	  _kill_flag(false),
	  _codec_context(nullptr),
	  _parser(nullptr),
	  _pkt(nullptr),
	  _frame(nullptr)
{
	_pkt   = ::av_packet_alloc();
	_frame = ::av_frame_alloc();
}

TranscodeDecoder::~TranscodeDecoder()
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
	OV_SAFE_FUNC(_pkt, nullptr, ::av_packet_free, &);
	OV_SAFE_FUNC(_parser, nullptr, ::av_parser_close, );

	_input_buffer.Clear();
}

std::shared_ptr<MediaTrack> &TranscodeDecoder::GetRefTrack()
{
	return _track;
}

cmn::Timebase TranscodeDecoder::GetTimebase()
{
	return GetRefTrack()->GetTimeBase();
}

void TranscodeDecoder::SetDecoderId(int32_t decoder_id)
{
	_decoder_id = decoder_id;
}

bool TranscodeDecoder::Configure(std::shared_ptr<MediaTrack> track)
{
	if (track == nullptr)
	{
		return false;
	}
	_track = track;

	auto name = ov::String::FormatString("dec_%s_t%d", cmn::GetCodecIdString(GetCodecID()), _track->GetId());
	auto urn = std::make_shared<info::ManagedQueue::URN>(_stream_info.GetApplicationInfo().GetVHostAppName(), _stream_info.GetName(), "trs", name);
	_input_buffer.SetUrn(urn);
	_input_buffer.SetThreshold(MAX_QUEUE_SIZE);

	try
	{
		_kill_flag = false;
		_codec_thread = std::thread(&TranscodeDecoder::CodecThread, this);
		pthread_setname_np(_codec_thread.native_handle(), name.CStr());

		// Initialize the codec and wait for completion.
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

	tc::TranscodeModules::GetInstance()->OnCreated(false, GetCodecID(), GetModuleID(), GetDeviceID());

	return true;
}

void TranscodeDecoder::Stop()
{
	_kill_flag = true;

	_input_buffer.Stop();

	if (_codec_thread.joinable())
	{
		_codec_thread.join();

		logtt(ov::String::FormatString("decoder %s thread has ended", cmn::GetCodecIdString(GetCodecID())).CStr());
	}

	tc::TranscodeModules::GetInstance()->OnDeleted(false, GetCodecID(), GetModuleID(), GetDeviceID());	
}

void TranscodeDecoder::SendBuffer(std::shared_ptr<const MediaPacket> packet)
{
	_input_buffer.Enqueue(std::move(packet));
}

void TranscodeDecoder::SetCompleteHandler(CompleteHandler complete_handler)
{
	_complete_handler = std::move(complete_handler);
}

void TranscodeDecoder::Complete(TranscodeResult result, std::shared_ptr<MediaFrame> frame)
{
	// Fault Injection for testing
	if (TranscodeFaultInjector::GetInstance()->IsEnabled())
	{
		if (TranscodeFaultInjector::GetInstance()->IsTriggered(
				TranscodeFaultInjector::ComponentType::DecoderComponent,
				TranscodeFaultInjector::IssueType::ProcessFailed,
				GetModuleID(),
				GetDeviceID()) == true)
		{
			result = TranscodeResult::DataError;
			frame  = nullptr;
		}

		if (TranscodeFaultInjector::GetInstance()->IsTriggered(
				TranscodeFaultInjector::ComponentType::DecoderComponent,
				TranscodeFaultInjector::IssueType::Lagging,
				GetModuleID(),
				GetDeviceID()) == true)
		{
			usleep(300 * 1000);	 // 300ms
		}
	}

	// Invoke callback function when encoding/decoding is completed.
	if (!_complete_handler)
	{
		return;
	}

	if (frame != nullptr)
	{
		frame->SetTrackId(_decoder_id);
	}

	_complete_handler(result, _decoder_id, std::move(frame));
}
