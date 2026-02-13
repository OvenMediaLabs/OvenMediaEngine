//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#define OV_LOG_TAG "Socket"

#define MAX_BUFFER_SIZE 4096

// If state is not <condition>, returns <return_value>
#define CHECK_STATE(condition, return_value)                                 \
	do                                                                       \
	{                                                                        \
		if (!(_state condition))                                             \
		{                                                                    \
			logaw("%s(): Invalid state: %s (expected: " #condition ") - %s", \
				  __FUNCTION__,                                              \
				  StringFromSocketState(_state),                             \
				  ToString().CStr());                                        \
			return return_value;                                             \
		}                                                                    \
	} while (false)

#define CHECK_STATE2(condition1, condition2, return_value)                                                     \
	do                                                                                                         \
	{                                                                                                          \
		SocketState __state;                                                                                   \
                                                                                                               \
		{                                                                                                      \
			std::lock_guard lock_guard(_state_mutex);                                                          \
			__state = _state;                                                                                  \
		}                                                                                                      \
                                                                                                               \
		if ((!(__state condition1)) && (!(__state condition2)))                                                \
		{                                                                                                      \
			logaw("%s(): Invalid state: %s (expected: _state " #condition1 " && _state " #condition2 ") - %s", \
				  __FUNCTION__,                                                                                \
				  StringFromSocketState(__state),                                                              \
				  ToString().CStr());                                                                          \
			return return_value;                                                                               \
		}                                                                                                      \
	} while (false)
