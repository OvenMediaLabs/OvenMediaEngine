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
		// deprecated_json_name is an old JSON name kept for backward compatibility.
		// During the deprecation window it is accepted on input and emitted on output alongside the current name,
		// and will be removed in a future release.
		// Supported only for scalar leaf values; `Item::AddChild()` throws a ConfigError otherwise.
		ItemName(const char *xml_name, const char *json_name, const char *deprecated_json_name);

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
			return (xml_name == name.xml_name) &&
				   (json_name == name.json_name) &&
				   (deprecated_json_name == name.deprecated_json_name);
		}

		ov::String xml_name;
		ov::String json_name;
		ov::String deprecated_json_name;

	protected:
		ItemName()					= default;

		bool _created_from_xml_name = false;
	};

	// Joins an item path and a child name into a dotted path, such as "playlists.options"
	inline ov::String MakeChildPath(const ov::String &parent_path, const ov::String &child_name)
	{
		return parent_path.IsEmpty()
				   ? child_name
				   : ov::String::FormatString("%s.%s", parent_path.CStr(), child_name.CStr());
	}
}  // namespace cfg
