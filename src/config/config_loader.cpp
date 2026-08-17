//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Gil Hoon Choi
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "config_loader.h"

#include "config_private.h"

namespace cfg
{
	ConfigLoader::ConfigLoader()
	{
	}

	ConfigLoader::ConfigLoader(const ov::String &config_path)
		: _config_path(config_path)
	{
	}

	ConfigLoader::~ConfigLoader()
	{
	}

	void ConfigLoader::Reset()
	{
		_document.reset();
		_config_path.Clear();
	}

	void ConfigLoader::Load()
	{
		if ((_config_path == nullptr) || _config_path.IsEmpty())
		{
			throw CreateConfigError("Config file path is empty");
		}

		pugi::xml_parse_result result = _document.load_file(_config_path);

		if (result == false)
		{
			throw CreateConfigError("Could not load config... reason: %s [%s]", result.description(), _config_path.CStr());
		}

		logtt("Config is loaded successfully: %s", _config_path.CStr());
	}

	const ov::String ConfigLoader::GetConfigPath() const noexcept
	{
		return _config_path;
	}

	void ConfigLoader::SetConfigPath(ov::String config_path)
	{
		_config_path = config_path;
	}
}  // namespace cfg
