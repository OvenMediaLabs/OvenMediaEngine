//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "extra_data.h"

#include <modules/json_serdes/converters.h>

namespace mon::alrt
{

	Json::Value ExtraData::ToJsonValue() const
	{
		Json::Value jv_root;

		// Codec Module
		if (_codec_module)
		{
			Json::Value jv_item	   = ::serdes::JsonFromCodecModule(_codec_module);
			jv_root["codecModule"] = jv_item;
		}

		// Output Profile
		if (_output_profile)
		{
			Json::Value jv_item		 = ::serdes::JsonFromOutputProfile(*_output_profile);
			jv_root["outputProfile"] = jv_item;
		}

		// Optional fields
		if (_source_track_id.has_value())
		{
			jv_root["sourceTrackId"] = _source_track_id.value();
		}
		if (_parent_source_track_id.has_value())
		{
			jv_root["parentSourceTrackId"] = _parent_source_track_id.value();
		}
		if (_error_count.has_value())
		{
			jv_root["errorCount"] = _error_count.value();
		}
		if (_error_evaluation_interval.has_value())
		{
			jv_root["errorEvaluationInterval"] = _error_evaluation_interval.value();
		}

		return jv_root;
	}
}  // namespace mon::alrt
