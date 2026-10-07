//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "transcoder_decoder.h"

#include <chrono>

#include <modules/bitstream/h264/h264_parser.h>

#include "codec/decoder/decoder_avcodec_audio.h"
#include "codec/decoder/decoder_avcodec_video.h"
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
		  cmn::GetCodecIdString(track->GetCodecId()),
		  hwaccels_enable ? "true" : "false",
		  hwaccles_modules.CStr());

	std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidate_modules = nullptr;
	candidate_modules = std::make_shared<std::vector<std::shared_ptr<info::CodecCandidate>>>();	

	// If the track is not video, the default module is the only candidate.
	if (cmn::IsVideoCodec(track->GetCodecId()) == false)
	{
		candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), cmn::MediaCodecModuleId::DEFAULT, 0));
		return candidate_modules;
	}


	ov::String configuration = "";
	if (hwaccels_enable == true)
	{
		configuration = hwaccles_modules.Trim();
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
			desire_modules.push_back(ov::String::FormatString("%s:%d", "NI", ALL_GPU_ID));
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
				if (gpu_id != ALL_GPU_ID && gpu_id != device_id)
				{
					continue;
				}

				if (tc::TranscodeModules::GetInstance()->GetModule(/*coder_type=*/false, track->GetCodecId(), module_id, device_id) == nullptr)
				{
					logtd("%s:%d cannot decode %s. Skip this module",
						  cmn::GetCodecModuleIdString(module_id),
						  device_id,
						  cmn::GetCodecIdString(track->GetCodecId()));

					continue;
				}

				candidate_modules->push_back(std::make_shared<info::CodecCandidate>(track->GetCodecId(), module_id, device_id));
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

#define CREATE_DECODER(CLS)                                                       \
	decoder = std::make_shared<CLS>(*info, candidate->GetCodecId());              \
	if (decoder != nullptr)                                                       \
	{                                                                             \
		decoder->SetDeviceID(candidate->GetDeviceId());                           \
		decoder->SetDecoderId(decoder_id);                                        \
		decoder->SetCompleteHandler(complete_handler);                            \
		track->SetCodecModuleId(decoder->GetModuleID());                          \
		track->SetCodecDeviceId(decoder->GetDeviceID());                          \
		if (decoder->Configure(track) == true)                                    \
		{                                                                         \
			if (TranscodeFaultInjector::GetInstance()->IsEnabled() == false ||    \
				TranscodeFaultInjector::GetInstance()->IsTriggered(               \
					TranscodeFaultInjector::ComponentType::DecoderComponent,      \
					TranscodeFaultInjector::IssueType::InitFailed,                \
					decoder->GetModuleID(),                                       \
					decoder->GetDeviceID()) == false)                             \
			{                                                                     \
				goto done;                                                        \
			}                                                                     \
		}                                                                         \
		decoder->Stop();                                                          \
		decoder = nullptr;                                                        \
	}

std::shared_ptr<TranscodeDecoder> TranscodeDecoder::Create(
	int32_t decoder_id,
	std::shared_ptr<info::Stream> info,
	std::shared_ptr<MediaTrack> track,
	std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidates,
	CompleteHandler complete_handler)
{
	std::shared_ptr<TranscodeDecoder> decoder = nullptr;
	for (auto &candidate : *candidates)
	{
		switch (candidate->GetModuleId())
		{
			case cmn::MediaCodecModuleId::DEFAULT:
				if (cmn::IsVideoCodec(candidate->GetCodecId()) == true)
				{
					CREATE_DECODER(AVCodecVideoDecoder)
				}
				else
				{
					CREATE_DECODER(AVCodecAudioDecoder)
				}
				break;
			
			default:
				break;
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
	  _kill_flag(false)
{
}

TranscodeDecoder::~TranscodeDecoder()
{
	Stop();

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
	// Set track information
	if (track == nullptr)
	{
		return false;
	}
	_track = track;

	// Set the input buffer information 
	auto name = ov::String::FormatString("dec_%s_t%d", cmn::GetCodecIdString(GetCodecID()), _track->GetId());
	auto urn = std::make_shared<info::ManagedQueue::URN>(_stream_info.GetApplicationInfo().GetVHostAppName(), _stream_info.GetName(), "trs", name);
	_input_buffer.SetUrn(urn);
	_input_buffer.SetThreshold(MAX_QUEUE_SIZE);


	// Start decoding thread
	try
	{
		_kill_flag = false;
		_codec_thread = std::thread(&TranscodeDecoder::ThreadLoop, this);
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
	if (_codec_thread.joinable())
	{
		_kill_flag = true;
		_input_buffer.Stop();
		_codec_thread.join();

		tc::TranscodeModules::GetInstance()->OnDeleted(false, GetCodecID(), GetModuleID(), GetDeviceID());

		logtt("decoder %s thread has ended", cmn::GetCodecIdString(GetCodecID()));
	}
}

// A keyframe, or an H.264 recovery point SEI: open GOP and intra refresh streams start there without an IDR.
static bool IsDecodeStartPoint(const std::shared_ptr<MediaPacket> &packet)
{
	if (packet->GetFlag() == MediaPacketFlag::Key)
	{
		return true;
	}

	const auto &data = packet->GetData();
	if ((packet->GetBitstreamFormat() != cmn::BitstreamFormat::H264_ANNEXB) || (data == nullptr))
	{
		return false;
	}

	return H264Parser::CheckAnnexBRecoveryPoint(data->GetDataAs<uint8_t>(), data->GetLength());
}

void TranscodeDecoder::ThreadLoop()
{
	ov::logger::ThreadHelper thread_helper;

	// Initialize the codec (and bitstream framer) and notify the main thread.
	if (_codec_init_event.Submit(Initialize()) == false)
	{
		_drain_event.Submit(false);
		return;
	}

	while (!_kill_flag)
	{
		auto packet = GetFramedPacket();
		if ((packet != nullptr) && (_keyframe_sent == false) && (GetMediaType() == cmn::MediaType::Video))
		{
			if (IsDecodeStartPoint(packet) == false)
			{
				// Undecodable before a start point (e.g. joined mid-GOP). Reported per packet so the last picture repeats on time,
				// instead of stalling and then bursting.
				Complete(TranscodeResult::NoData, MediaFrame::Create(cmn::MediaType::Video, packet->GetDts()));
				packet = nullptr;
			}
			else
			{
				_keyframe_sent = true;
			}
		}

		if (packet != nullptr)
		{
			auto sent = SendPacket(packet);
			
			if(sent.result != TranscodeResult::Again)
			{
				Complete(sent.result, std::move(sent.frame));
			}
		}

		while (!_kill_flag)
		{
			auto received = ReceiveFrame();
			if (received.result == TranscodeResult::FormatChanged ||
				received.result == TranscodeResult::DataReady)
			{
				Complete(received.result, std::move(received.frame));

				// Keep draining to check whether more frames are pending.
				continue;
			}
			else if (received.result == TranscodeResult::Again)
			{
				// The decoder is not ready to hand over a frame; leave the loop.
				break;
			}
			else if (received.result == TranscodeResult::NoData ||
					 received.result == TranscodeResult::DataError)
			{
				Complete(received.result, std::move(received.frame));

				// The frame handed over is empty or missing, so stop draining rather than
				// asking the decoder again - it would keep returning the same result.
				break;
			}
			else
			{
				// Unexpected result; stop draining.
				logtw("Unexpected result from ReceiveFrame(): %d", ov::ToUnderlyingType(received.result));
				break;
			}
		}

		// Everything queued before the marker has been decoded.
		if (_drain_reached == true)
		{
			FlushCodec();
			break;
		}
	}

	_drain_event.Submit(_drain_reached);

	Uninitialize();
}

std::optional<std::shared_ptr<const MediaPacket>> TranscodeDecoder::DequeueInput()
{
	auto obj = _input_buffer.Dequeue();
	if ((obj.has_value() == true) && (obj.value() == nullptr))
	{
		_drain_reached = true;
		return std::nullopt;
	}

	return obj;
}

void TranscodeDecoder::FlushCodec()
{
	// Nothing was fed, so nothing is held.
	if ((GetMediaType() == cmn::MediaType::Video) && (_keyframe_sent == false))
	{
		return;
	}

	if (SendEOS() == false)
	{
		return;
	}

	// Bounded, so a codec stuck on bad data cannot hold the caller.
	int frames = 0;
	while ((frames < kMaxDrainFrames) && (_kill_flag == false))
	{
		auto received = ReceiveFrame();
		if ((received.result != TranscodeResult::DataReady) && (received.result != TranscodeResult::FormatChanged))
		{
			// Nothing more to hand out (or an error).
			break;
		}

		Complete(received.result, std::move(received.frame));
		frames++;
	}

	logtd("decoder %s flushed %d frames", cmn::GetCodecIdString(GetCodecID()), frames);
}

void TranscodeDecoder::DrainAndStop()
{
	if (_codec_thread.joinable() == true)
	{
		// A backlog this deep would only run out the timeout.
		auto queued = _input_buffer.Size();
		if (queued > MAX_QUEUE_SIZE)
		{
			logtw("decoder %s skips the drain: %zu packets queued (threshold %d); they are dropped",
				  cmn::GetCodecIdString(GetCodecID()), queued, MAX_QUEUE_SIZE);
		}
		else
		{
			auto started = std::chrono::steady_clock::now();

			// FIFO: the thread meets the marker only after every packet sent before it.
			_input_buffer.Enqueue(nullptr);

			bool drained	= _drain_event.GetFor(kDrainTimeoutMs);
			auto elapsed_ms = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());

			if (drained == true)
			{
				logtd("decoder %s drained %zu packets in %" PRId64 " ms", cmn::GetCodecIdString(GetCodecID()), queued, elapsed_ms);
			}
			else if (elapsed_ms < kDrainTimeoutMs)
			{
				// The thread ended before reaching the marker (e.g. a failed initialization).
				logtd("decoder %s had already stopped; nothing to drain", cmn::GetCodecIdString(GetCodecID()));
			}
			else
			{
				logtw("decoder %s could not drain within %u ms; the rest is dropped", cmn::GetCodecIdString(GetCodecID()), kDrainTimeoutMs);
			}
		}
	}

	Stop();
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
