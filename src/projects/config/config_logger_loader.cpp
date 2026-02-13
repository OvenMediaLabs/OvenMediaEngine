//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Gil Hoon Choi
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "config_logger_loader.h"

#include "config_private.h"

namespace cfg
{
	std::shared_ptr<LoggerTagInfo> ParseTag(pugi::xml_node tag_node);

	ConfigLoggerLoader::ConfigLoggerLoader()
	{
	}

	ConfigLoggerLoader::ConfigLoggerLoader(const ov::String config_path)
		: ConfigLoader(config_path)
	{
	}

	ConfigLoggerLoader::~ConfigLoggerLoader()
	{
	}

	void ConfigLoggerLoader::Parse()
	{
		ConfigLoader::Load();

		pugi::xml_node logger_node = _document.child("Logger");

		if (logger_node.empty())
		{
			throw CreateConfigError("Could not found config: <Logger>");
		}

		pugi::xml_node tag_node = logger_node.child("Tag");

		if (tag_node.empty())
		{
			throw CreateConfigError("Could not found config: <Tag>");
		}

		while (tag_node)
		{
			std::shared_ptr<LoggerTagInfo> tag_info = ParseTag(tag_node);

			if (tag_info != nullptr)
			{
				_tags.push_back(tag_info);
			}

			tag_node = tag_node.next_sibling();
		}

		_log_path = logger_node.child_value("Path");
		_version  = logger_node.attribute("version").value();
	}

	void ConfigLoggerLoader::Reset()
	{
		_tags.clear();

		ConfigLoader::Reset();
	}

	std::vector<std::shared_ptr<LoggerTagInfo>> ConfigLoggerLoader::GetTags() const noexcept
	{
		return _tags;
	}

	ov::String ConfigLoggerLoader::GetLogPath() const noexcept
	{
		return _log_path;
	}

	ov::String ConfigLoggerLoader::GetVersion() const noexcept
	{
		return _version;
	}

	std::shared_ptr<LoggerTagInfo> ParseTag(pugi::xml_node tag_node)
	{
		std::shared_ptr<LoggerTagInfo> tag_info = std::make_shared<LoggerTagInfo>();

		ov::String name							= ConfigUtility::StringFromAttribute(tag_node.attribute("name"));
		ov::String level						= ConfigUtility::StringFromAttribute(tag_node.attribute("level"));

		if (LoggerTagInfo::ValidateLogLevel(level) == false)
		{
			throw CreateConfigError("Invalid log level: %s (Tag name: [%s])", level.CStr(), name.CStr());
		}

		tag_info->SetName(name);
		tag_info->SetLevel(LoggerTagInfo::OVLogLevelFromString(level).value_or(OVLogLevelInformation));

		return tag_info;
	}
}  // namespace cfg
