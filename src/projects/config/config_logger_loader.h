//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Gil Hoon Choi
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/logger_tag_info.h>

#include "config_loader.h"

namespace cfg
{
	class ConfigLoggerLoader : public ConfigLoader
	{
	public:
		ConfigLoggerLoader();
		explicit ConfigLoggerLoader(const ov::String config_path);
		virtual ~ConfigLoggerLoader();

		MAY_THROWS(cfg::ConfigError)
		void Parse() override;

		void Reset();

		std::vector<std::shared_ptr<LoggerTagInfo>> GetTags() const noexcept;
		ov::String GetLogPath() const noexcept;
		ov::String GetVersion() const noexcept;
		bool IsFileEnabled() const noexcept;

	private:
		std::vector<std::shared_ptr<LoggerTagInfo>> _tags;
		ov::String _log_path;
		ov::String _version = "1.0";
		bool _file_enabled = true;
	};
}  // namespace cfg