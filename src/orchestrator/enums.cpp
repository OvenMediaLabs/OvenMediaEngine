//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "enums.h"

#define GET_TYPE_NAME(enum_type, name, value)      \
	do                                             \
	{                                              \
		if (OV_CHECK_FLAG(value, enum_type::name)) \
		{                                          \
			list.push_back(#name);                 \
		}                                          \
	} while (false)

namespace ocst
{
	ov::String GetModuleTypeName(ModuleType type)
	{
		std::vector<ov::String> list;

		GET_TYPE_NAME(ModuleType, Unknown, type);
		GET_TYPE_NAME(ModuleType, PullProvider, type);
		GET_TYPE_NAME(ModuleType, PushProvider, type);
		GET_TYPE_NAME(ModuleType, MediaRouter, type);
		GET_TYPE_NAME(ModuleType, Transcoder, type);
		GET_TYPE_NAME(ModuleType, Publisher, type);

		if (list.size() == 0)
		{
			// Unknown type name (not handled)
			OV_ASSERT2(false);
			return "N/A";
		}

		return ov::String::Join(list, " | ");
	}
}  // namespace ocst
