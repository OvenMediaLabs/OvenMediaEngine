//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "codec.h"
#include "common.h"

namespace serdes
{
	Json::Value JsonFromCodecModule(const std::shared_ptr<info::CodecModule> &module)
	{
		Json::Value response(Json::ValueType::objectValue);

		serdes::SetString(response, "name", module->GetName(), Optional::False);
		serdes::SetString(response, "displayName", module->GetDisplayName(), Optional::False);
		serdes::SetString(response, "mediaType", cmn::GetMediaTypeString(module->GetMediaType()), Optional::False);
		serdes::SetString(response, "module", cmn::GetCodecModuleIdString(module->GetModuleId()), Optional::False);
		serdes::SetInt(response, "id", module->GetDeviceId());
		serdes::SetString(response, "busId", module->GetBusId(), Optional::False);
		serdes::SetBool(response, "isDefault", module->isDefault());
		serdes::SetBool(response, "isEncoder", module->isEncoder());
		serdes::SetBool(response, "isDecoder", module->isDecoder());
		serdes::SetBool(response, "isHwAccel", module->isHwAccel());
		
		{
			Json::Value supported_codecs(Json::ValueType::arrayValue);
			for (const auto &supported_codec : module->GetSupportedCodecs())
			{
				supported_codecs.append(cmn::GetCodecIdString(supported_codec));
			}
			response["supportedCodecs"] = supported_codecs;
		}

		{
			Json::Value metrics(Json::ValueType::objectValue);
			{
				Json::Value metrics_active(Json::ValueType::objectValue);
				serdes::SetInt(metrics_active, "decoder", module->GetMetrics()._active_decoder);
				serdes::SetInt(metrics_active, "encoder", module->GetMetrics()._active_encoder);
				metrics["active"] = metrics_active;
			}
			response["metrics"] = metrics;	
		}

		return response;
	}
}  // namespace serdes