//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

/// Run `statement` if the `case_value` is matched
///
/// @param case_value The value to match
/// @param statement The statement to execute if the case is matched
///
/// @example
/// ```
/// OV_CASE_BREAK(AudioChannel::FrontLeft, _channel = "FrontLeft")
/// ```
///
/// This will return "FrontLeft" if the value is `AudioChannel::FrontLeft`
#define OV_CASE_BREAK(case_value, statement) \
	case case_value:                         \
		statement;                           \
		break

/// Return `return_value` if the `case_value` is matched
///
/// @param case_value The value to match
/// @param return_value The value to return if the case is matched
///
/// @example
/// ```
/// OV_CASE_RETURN(AudioChannel::FrontLeft, "FrontLeft")
/// ```
///
/// This will return "FrontLeft" if the value is `AudioChannel::FrontLeft`
#define OV_CASE_RETURN(case_value, return_value) \
	case case_value:                             \
		return return_value

/// Return the string representation of the enum value
///
/// @param type The enum type
/// @param enum_value The enum value to match
///
/// @example
/// ```
/// OV_CASE_RETURN_ENUM_STRING(AudioChannel, FrontLeft)
/// ```
///
/// This will return "FrontLeft" if the value is `AudioChannel::FrontLeft`
#define OV_CASE_RETURN_ENUM_STRING(type, enum_value) \
	case type::enum_value:                           \
		return #enum_value

/// Return the string representation of the enum value
///
/// @param condition The condition to check
/// @param return_value The value to return if the condition is true
///
/// @example
/// ```
/// OV_IF_RETURN(value == AudioChannel::FrontLeft, "FrontLeft");
/// ```
#define OV_IF_RETURN(condition, return_value) \
	do                                        \
	{                                         \
		if (condition)                        \
		{                                     \
			return return_value;              \
		}                                     \
	} while (false)
