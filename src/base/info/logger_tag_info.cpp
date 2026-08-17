//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Gil Hoon Choi
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "logger_tag_info.h"

LoggerTagInfo::LoggerTagInfo()
{
}

LoggerTagInfo::~LoggerTagInfo()
{
}

const ov::String LoggerTagInfo::GetName() const noexcept
{
	return _name;
}

void LoggerTagInfo::SetName(ov::String name)
{
	_name = name;
}

const OVLogLevel LoggerTagInfo::GetLevel() const noexcept
{
	return _level;
}

void LoggerTagInfo::SetLevel(OVLogLevel level)
{
	_level = level;
}

/*
	OVLogLevelTrace,
	OVLogLevelDebug,
	OVLogLevelInformation,
	OVLogLevelWarning,
	OVLogLevelError,
	OVLogLevelCritical
 */

const char *LoggerTagInfo::StringFromOVLogLevel(OVLogLevel log_level) noexcept
{
	switch (log_level)
	{
		case OVLogLevelTrace:
			return "trace";
		case OVLogLevelDebug:
			return "debug";
		case OVLogLevelInformation:
			return "info";
		case OVLogLevelWarning:
			return "warn";
		case OVLogLevelError:
			return "error";
		case OVLogLevelCritical:
			return "critical";
		default:
			return "unknown";
	}
}

std::optional<OVLogLevel> LoggerTagInfo::OVLogLevelFromString(ov::String level_string) noexcept
{
	if (level_string == "trace")
	{
		return OVLogLevelTrace;
	}
	else if (level_string == "debug")
	{
		return OVLogLevelDebug;
	}
	else if (level_string == "info")
	{
		return OVLogLevelInformation;
	}
	else if (level_string == "warn")
	{
		return OVLogLevelWarning;
	}
	else if (level_string == "error")
	{
		return OVLogLevelError;
	}
	else if (level_string == "critical")
	{
		return OVLogLevelCritical;
	}

	return std::nullopt;
}

const bool LoggerTagInfo::ValidateLogLevel(ov::String level_string)
{
	return OVLogLevelFromString(level_string).has_value();
}
