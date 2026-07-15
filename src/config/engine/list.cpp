//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./list.h"

#include "./item.h"

namespace cfg
{
	void CopyValueToXmlNode(pugi::xml_node &node, const ov::String &item_name, const Json::Value &value, bool include_default_values)
	{
		node.text().set(ov::Converter::ToString(value));
	}

	void CopyValueToXmlNode(pugi::xml_node &node, const ov::String &item_name, const Item *value, bool include_default_values)
	{
		value->ToXml(node, include_default_values);
	}

	void CopyValueToJson(Json::Value &json, const Item *value, bool original_value, bool include_default_values)
	{
		value->ToJson(json, original_value, include_default_values);
	}

	void SetItemDataFromDataSource(Item *item, const ItemName &item_name, const DataSource &data_source)
	{
		item->SetItemName(item_name);
		item->FromDataSource(data_source);
	}
}  // namespace cfg
