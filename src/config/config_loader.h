//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Gil Hoon Choi
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <pugixml-1.9/src/pugixml.hpp>

#include "engine/config_error.h"

namespace cfg
{
	class ConfigLoader
	{
	public:
		ConfigLoader();
		explicit ConfigLoader(const ov::String &config_path);
		virtual ~ConfigLoader();

		MAY_THROWS(cfg::ConfigError)
		virtual void Parse() = 0;
		void Reset();

		const ov::String GetConfigPath() const noexcept;
		void SetConfigPath(ov::String config_path);

	protected:
		MAY_THROWS(cfg::ConfigError)
		void Load();

		pugi::xml_document _document;

	private:
		ov::String _config_path;
	};
}  // namespace cfg