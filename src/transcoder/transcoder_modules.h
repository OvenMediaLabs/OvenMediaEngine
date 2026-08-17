//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/info/codec.h"
#include <base/ovlibrary/ovlibrary.h>

namespace tc
{
	class TranscodeModules : public ov::Singleton<TranscodeModules>
	{
	public:
		TranscodeModules();
		~TranscodeModules() = default;

		void Register(const info::CodecModule &codec_info)
		{
			_modules.push_back(std::make_shared<info::CodecModule>(codec_info));
		}

		const std::vector<std::shared_ptr<info::CodecModule>> &GetModules() const
		{
			return _modules;
		}

		std::shared_ptr<info::CodecModule> GetModule(bool coder_type, cmn::MediaCodecId codec_id, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id) const
		{
			auto media_type = cmn::IsVideoCodec(codec_id) ? cmn::MediaType::Video : cmn::IsImageCodec(codec_id) ? cmn::MediaType::Video
																												: cmn::MediaType::Audio;

			for (const auto &codec : _modules)
			{
				if (codec->GetMediaType() == media_type &&
					codec->GetModuleId() == module_id &&
					codec->GetDeviceId() == device_id &&
					(coder_type ? codec->isEncoder() : codec->isDecoder()) &&
					std::find(codec->GetSupportedCodecs().begin(), codec->GetSupportedCodecs().end(), codec_id) != codec->GetSupportedCodecs().end())
				{
					return codec;
				}
			}

			return nullptr;
		}

		// coder_type: true - encoder, false - decoder
		bool OnCreated(bool coder_type, cmn::MediaCodecId codec_id, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id);
		bool OnDeleted(bool coder_type, cmn::MediaCodecId codec_id, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id);

		ov::String GetInfoString() const;

	private:
		std::vector<std::shared_ptr<info::CodecModule>> _modules;
	};
}  // namespace tc
