//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "amf_textdata_event.h"

bool AmfTextDataEvent::IsMatch(const ov::String &amf_type)
{
	return amf_type.UpperCaseString() == ov::String("onTextData").UpperCaseString();
}

std::shared_ptr<AmfTextDataEvent> AmfTextDataEvent::Create()
{
	return std::make_shared<AmfTextDataEvent>();
}

std::shared_ptr<AmfTextDataEvent> AmfTextDataEvent::Parse(const std::shared_ptr<ov::Data> &data)
{
	if (data == nullptr)
	{
		return nullptr;
	}

	auto object = std::make_shared<AmfTextDataEvent>();

	return object;
}

bool AmfTextDataEvent::IsValid(const Json::Value &json)
{
	if (json["data"].isNull() || !json["data"].isObject())
	{
		return false;
	}

	return true;
}

std::shared_ptr<AmfTextDataEvent> AmfTextDataEvent::Parse(const Json::Value &json)
{
	if (json["data"].isNull() || !json["data"].isObject())
	{
		return nullptr;
	}

	auto object = std::make_shared<AmfTextDataEvent>();

	auto data	= json["data"];
	for (const auto &key : data.getMemberNames())
	{
		auto value = data[key];

		if (value.isBool())
		{
			object->Append(key.c_str(), value.asBool());
		}
		else if (value.isInt() || value.isUInt() || value.isDouble())
		{
			object->Append(key.c_str(), value.asDouble());
		}
		else if (value.isString())
		{
			object->Append(key.c_str(), value.asString().c_str());
		}
	}

	return object;
}

bool AmfTextDataEvent::IsValid(const pugi::xml_node &xml)
{
	ov::String amf_type = xml.child_value("AmfType");
	if (!(amf_type.UpperCaseString() == "ONTEXTDATA"))
	{
		return false;
	}

	auto data_node = xml.child("Data");
	if (data_node.empty())
	{
		return false;
	}

	return true;
}


std::shared_ptr<AmfTextDataEvent> AmfTextDataEvent::Parse(const pugi::xml_node &xml)
{
	auto object = std::make_shared<AmfTextDataEvent>();
	if (object == nullptr)
	{
		return nullptr;
	}

	auto data_node = xml.child("Data");
	if (data_node.empty())
	{
		// logte("AMF event must have Data node");
		return nullptr;
	}

	size_t child_node_count = std::distance(data_node.begin(), data_node.end());
	if (child_node_count > 0)
	{
		// Complex data
		for (pugi::xml_node data_child_node = data_node.first_child(); data_child_node; data_child_node = data_child_node.next_sibling())
		{
			ov::String key	= data_child_node.name();
			ov::String type = data_child_node.attribute("type").as_string();
			if (type.IsEmpty())
			{
				type = "string";
			}
			ov::String value = data_child_node.child_value();

			if (type == "double" || type == "number" || type == "float")
			{
				double double_value = atof(value.CStr());
				object->Append(key.CStr(), double_value);
			}
			else if (type == "boolean" || type == "bool")
			{
				bool bool_value = (strcmp(value.CStr(), "true") == 0);
				object->Append(key.CStr(), bool_value);
			}
			else
			{
				object->Append(key.CStr(), value.CStr());
			}
		}
	}

	return object;
}

AmfTextDataEvent::AmfTextDataEvent()
{
	_arrays = nullptr;
}

std::shared_ptr<ov::Data> AmfTextDataEvent::Serialize() const
{
	AmfDocument doc;
	doc.AppendProperty(AmfProperty("onTextData"));

	auto arrays = std::make_shared<AmfEcmaArray>();

	for (const auto &[name, property] : _arrays->GetPropertyPairs())
	{
		if (property.GetType() == AmfTypeMarker::String)
		{
			ov::String str_value = property.GetString();
			str_value			 = str_value.Replace("${EpochTime}", ov::String::FormatString("%" PRId64, ov::Time::GetTimestampInMs()));

			arrays->Append(name.CStr(), AmfProperty(str_value.CStr()));
		}
		else
		{
			arrays->Append(name.CStr(), property);
		}
	}

	if (arrays != nullptr)
	{
		doc.AppendProperty(*arrays.get());
	}

	ov::ByteStream byte_stream;
	if (doc.Encode(byte_stream) == false)
	{
		return nullptr;
	}

	return byte_stream.GetDataPointer();
}