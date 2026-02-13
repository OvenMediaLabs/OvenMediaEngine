//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "transcoder_modules.h"

#include "transcoder_gpu.h"
#include "transcoder_private.h"

namespace tc
{
	TranscodeModules::TranscodeModules()
	{
		// TODO(Keukhan) : Currently, software and hardware modules are defined manually, but in the future,
		// it should be improved so that they are automatically registered based on the available codecs.

		// --------------------------------------------------------------------------
		// Video Codecs
		// --------------------------------------------------------------------------
		Register(info::CodecModule("FFmpeg Video Codecs", cmn::MediaType::Video, cmn::MediaCodecModuleId::DEFAULT, 0, "-",
						   {cmn::MediaCodecId::H264, cmn::MediaCodecId::H265, cmn::MediaCodecId::Vp8},
						   true, true, false, false));

		Register(info::CodecModule("Open Source H.264 Codec", cmn::MediaType::Video, cmn::MediaCodecModuleId::OPENH264, 0, "-",
						   {cmn::MediaCodecId::H264},
						   true, false, true, false));

#ifdef THIRDP_LIBX264_ENABLED
		Register(info::CodecModule("x264 H.264 Codec", cmn::MediaType::Video, cmn::MediaCodecModuleId::X264, 0, "-",
						   {cmn::MediaCodecId::H264},
						   false, false, true, false));
#endif

		Register(info::CodecModule("WebM VP8/VP9 Codec SDK", cmn::MediaType::Video, cmn::MediaCodecModuleId::LIBVPX, 0, "-",
						   {cmn::MediaCodecId::Vp8},
						   true, false, true, false));

		// --------------------------------------------------------------------------
		// Audio Codecs
		// --------------------------------------------------------------------------
		Register(info::CodecModule("FFmpeg Audio Codecs", cmn::MediaType::Audio, cmn::MediaCodecModuleId::DEFAULT, 0, "-",
						   {cmn::MediaCodecId::Aac, cmn::MediaCodecId::Opus},
						   true, true, false, false));

		Register(info::CodecModule("Fraunhofer FDK AAC", cmn::MediaType::Audio, cmn::MediaCodecModuleId::FDKAAC, 0, "-",
						   {cmn::MediaCodecId::Aac},
						   true, false, true, false));

		Register(info::CodecModule("Opus Interactive Audio Codec", cmn::MediaType::Audio, cmn::MediaCodecModuleId::LIBOPUS, 0, "-",
						   {cmn::MediaCodecId::Opus},
						   true, false, true, false));

		// --------------------------------------------------------------------------
		// Image Codecs
		// --------------------------------------------------------------------------
		Register(info::CodecModule("FFmpeg Image Codec", cmn::MediaType::Video, cmn::MediaCodecModuleId::DEFAULT, 0, "-",
						   {cmn::MediaCodecId::Jpeg, cmn::MediaCodecId::Png, cmn::MediaCodecId::Webp},
						   true, false, true, false));

		// --------------------------------------------------------------------------
		// Video Codecs - Hardware Accelerated
		// --------------------------------------------------------------------------
#ifdef HWACCELS_NVIDIA_ENABLED
		{
			auto module_id = cmn::MediaCodecModuleId::NVENC;
			for (int32_t i = 0; i < TranscodeGPU::GetInstance()->GetDeviceCount(module_id); i++)
			{
				ov::String device_name = TranscodeGPU::GetInstance()->GetDeviceDisplayName(module_id, i);
				ov::String bus_id	   = TranscodeGPU::GetInstance()->GetDeviceBusId(module_id, i);

				Register(info::CodecModule(device_name, cmn::MediaType::Video, module_id, i, bus_id,
								   {cmn::MediaCodecId::H264, cmn::MediaCodecId::H265},
								   false, true, true, true));

				Register(info::CodecModule(device_name, cmn::MediaType::Audio, module_id, i, bus_id,
								   {cmn::MediaCodecId::Whisper},
								   false, false, true, true));
			}
		}
#endif

#ifdef HWACCELS_XMA_ENABLED
		{
			auto module_id = cmn::MediaCodecModuleId::XMA;
			for (int32_t i = 0; i < TranscodeGPU::GetInstance()->GetDeviceCount(module_id); i++)
			{
				ov::String device_name = TranscodeGPU::GetInstance()->GetDeviceDisplayName(module_id, i);
				ov::String bus_id	   = TranscodeGPU::GetInstance()->GetDeviceBusId(module_id, i);

				Register(info::CodecModule(device_name, cmn::MediaType::Video, module_id, i, bus_id,
								   {cmn::MediaCodecId::H264, cmn::MediaCodecId::H265},
								   false, true, true, true));
			}
		}
#endif
	}

	ov::String TranscodeModules::GetInfoString() const
	{
		ov::String info;
		for (const auto &codec : _modules)
		{
			info += codec->GetInfoString() + "\n";
		}
		return info;
	}

	// TODO(Keukhan) : In the future, this function should be moved to the monitoring instance.
	bool TranscodeModules::OnCreated(bool coder_type, cmn::MediaCodecId codec_id, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id)
	{
		auto module = GetModule(coder_type, codec_id, module_id, device_id);
		if (module == nullptr)
		{
			logtw("Cannot find the codec to activate. coder_type(%s), codec_id(%s), module_id(%s), device_id(%d)",
				  coder_type ? "Encoder" : "Decoder",
				  cmn::GetCodecIdString(codec_id),
				  cmn::GetCodecModuleIdString(module_id),
				  device_id);

			return false;
		}

		(coder_type == true) ? module->GetMetrics().IncreaseActiveEncoder() : module->GetMetrics().IncreaseActiveDecoder();

		// logtw("\n%s", tc::TranscodeModules::GetInstance()->GetInfoString().CStr());

		return true;
	}

	bool TranscodeModules::OnDeleted(bool coder_type, cmn::MediaCodecId codec_id, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id)
	{
		auto module = GetModule(coder_type, codec_id, module_id, device_id);
		if (module == nullptr)
		{
			logtw("Cannot find the codec to deactivate. coder_type(%s), codec_id(%s), module_id(%s), device_id(%d)",
				  coder_type ? "Encoder" : "Decoder",
				  cmn::GetCodecIdString(codec_id),
				  cmn::GetCodecModuleIdString(module_id),
				  device_id);

			return false;
		}

		(coder_type == true) ? module->GetMetrics().DecreaseActiveEncoder() : module->GetMetrics().DecreaseActiveDecoder();

		// logtw("\n%s", tc::TranscodeModules::GetInstance()->GetInfoString().CStr());

		return true;
	}
}  // namespace tc
