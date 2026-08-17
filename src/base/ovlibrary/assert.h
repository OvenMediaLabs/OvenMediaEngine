//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <cstdlib>

#include "log.h"
#include "stack_trace.h"

#ifdef DEBUG
#	define OV_ASSERT_TO_STRING(x)				# x
#	define OV_ASSERT_INTERNAL(expression, format, ...) \
	do \
	{ \
		if(!(expression)) \
		{ \
			loge("Assertion", "Assertion failed: %s:%d\n\tExpression: %s" format "\n\tStack trace:\n%s", \
				__FILE__, __LINE__, \
				OV_ASSERT_TO_STRING(expression), \
				## __VA_ARGS__, \
				ov::StackTrace::GetStackTrace().CStr() \
				); \
			::abort(); \
		} \
	} \
	while(false)

#	define OV_ASSERT(expression, format, ...)	OV_ASSERT_INTERNAL(expression, "\n\tMessage: " format, ## __VA_ARGS__)
#	define OV_ASSERT2(expression)				OV_ASSERT_INTERNAL(expression, "")
#else // DEBUG
#	define OV_ASSERT(expression, format, ...)
#	define OV_ASSERT2(expression)
#endif // DEBUG