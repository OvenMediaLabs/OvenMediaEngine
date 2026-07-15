//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "item_name.h"

namespace cfg
{
	ItemName::ItemName(const char *xml_name)
		: xml_name(xml_name)
	{
		// Create a name of key of JSON from xml_name
		if (this->xml_name.GetLength() > 0)
		{
			json_name.AppendFormat("%c%s", ::tolower(xml_name[0]), xml_name + 1);
		}

		_created_from_xml_name = true;
	}

	ItemName::ItemName(const char *xml_name, const char *json_name)
		: xml_name(xml_name),
		  json_name(json_name)
	{
	}

	ov::String ItemName::ToString() const
	{
		ov::String name = xml_name.CStr();

		if (_created_from_xml_name == false)
		{
			name.AppendFormat("/%s", json_name.CStr());
		}

		return name;
	}
}  // namespace cfg
