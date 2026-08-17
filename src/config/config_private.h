//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Gil Hoon Choi
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "utilities/config_utility.h"

#define OV_LOG_TAG "Config"

#define OV_LOG_PREFIX_FORMAT "[%p] "
#define OV_LOG_PREFIX_VALUE this

#define HANDLE_CAST_EXCEPTION(value_type, prefix, ...) \
	logat(                                             \
		prefix                                         \
		"Could not convert value:\n"                   \
		"\tType: %s\n"                                 \
		"\tFrom: %s\n"                                 \
		"\t  To: %s",                                  \
		##__VA_ARGS__,                                 \
		StringFromValueType(value_type),               \
		cast_exception.from.CStr(),                    \
		cast_exception.to.CStr());                     \
                                                       \
	throw CreateConfigError(                           \
		prefix                                         \
		"Could not convert value - from: %s, to: %s",  \
		##__VA_ARGS__,                                 \
		cast_exception.from.CStr(),                    \
		cast_exception.to.CStr())
