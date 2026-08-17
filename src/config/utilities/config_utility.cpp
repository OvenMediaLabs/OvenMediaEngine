//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Gil Hoon Choi
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "config_utility.h"

ov::String ConfigUtility::StringFromNode(pugi::xml_node node)
{
	return StringFromNode(node, "");
}

ov::String ConfigUtility::StringFromNode(pugi::xml_node node, const char *default_value)
{
	if (node.empty() || node.text().empty())
	{
		return default_value;
	}

	return ov::String::FormatString("%s", node.text().as_string(default_value));
}

int ConfigUtility::IntFromNode(pugi::xml_node node)
{
	return IntFromNode(node, 0);
}

int ConfigUtility::IntFromNode(pugi::xml_node node, int default_value)
{
	if (node.empty() || node.text().empty())
	{
		return default_value;
	}

	return node.text().as_int(default_value);
}

float ConfigUtility::FloatFromNode(pugi::xml_node node)
{
	return FloatFromNode(node, 0.0);
}

float ConfigUtility::FloatFromNode(pugi::xml_node node, float default_value)
{
	if (node.empty() || node.text().empty())
	{
		return default_value;
	}

	return node.text().as_float(default_value);
}

bool ConfigUtility::BoolFromNode(pugi::xml_node node)
{
	return BoolFromNode(node, true);
}

bool ConfigUtility::BoolFromNode(pugi::xml_node node, bool default_value)
{
	if (node.empty() || node.text().empty())
	{
		return default_value;
	}

	return node.text().as_bool(default_value);
}

ov::String ConfigUtility::StringFromAttribute(pugi::xml_attribute attribute)
{
	return StringFromAttribute(attribute, "");
}

ov::String ConfigUtility::StringFromAttribute(pugi::xml_attribute attribute, const char *default_value)
{
	if (attribute.empty())
	{
		return default_value;
	}

	return ov::String::FormatString("%s", attribute.as_string(default_value));
}