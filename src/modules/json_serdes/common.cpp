//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./common.h"

namespace serdes
{
	void SetTimestamp(Json::Value &parent_object, const char *key, const std::chrono::system_clock::time_point &time_point)
	{
		parent_object[key] = ov::Converter::ToISO8601String(time_point).CStr();
	}
}  // namespace serdes