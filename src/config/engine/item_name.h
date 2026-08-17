//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <ctype.h>

namespace cfg
{
	class Item;
	class ListInterface;
	class Child;

	enum class DataType
	{
		Xml,
		Json
	};

	struct ItemName
	{
		friend class Item;
		friend class ListInterface;
		friend class Child;

	public:
		ItemName(const char *xml_name);
		ItemName(const char *xml_name, const char *json_name);

		ov::String ToString() const;

		const ov::String &GetName(DataType type) const
		{
			switch (type)
			{
				case DataType::Xml:
					[[fallthrough]];
				default:
					return xml_name;

				case DataType::Json:
					return json_name;
			}
		}

		bool operator==(const ItemName &name) const
		{
			return (xml_name == name.xml_name) && (json_name == name.json_name);
		}

		ov::String xml_name;
		ov::String json_name;

	protected:
		ItemName()					= default;

		bool _created_from_xml_name = false;
	};
}  // namespace cfg
