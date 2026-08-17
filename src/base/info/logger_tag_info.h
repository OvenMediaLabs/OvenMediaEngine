//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Gil Hoon Choi
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"

class LoggerTagInfo
{
public:
	LoggerTagInfo();
	virtual ~LoggerTagInfo();

	const ov::String GetName() const noexcept;
	void SetName(ov::String name);

	const OVLogLevel GetLevel() const noexcept;
	void SetLevel(OVLogLevel level);

	// Utilities
	static const char *StringFromOVLogLevel(OVLogLevel log_level) noexcept;
	static std::optional<OVLogLevel> OVLogLevelFromString(ov::String level_string) noexcept;
	static const bool ValidateLogLevel(ov::String level_string);

private:
	ov::String _name;
	OVLogLevel _level;
};
