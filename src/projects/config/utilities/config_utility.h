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

class ConfigUtility
{
public:
	static ov::String StringFromNode(pugi::xml_node node);
	static ov::String StringFromNode(pugi::xml_node node, const char *default_value);
	static int IntFromNode(pugi::xml_node node);
	static int IntFromNode(pugi::xml_node node, int default_value);
	static float FloatFromNode(pugi::xml_node node);
	static float FloatFromNode(pugi::xml_node node, float default_value);
	static bool BoolFromNode(pugi::xml_node node);
	static bool BoolFromNode(pugi::xml_node node, bool default_value);

	static ov::String StringFromAttribute(pugi::xml_attribute attribute);
	static ov::String StringFromAttribute(pugi::xml_attribute attribute, const char *default_value);
};
