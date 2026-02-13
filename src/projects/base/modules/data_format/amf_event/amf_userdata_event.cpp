//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "amf_userdata_event.h"

bool AmfUserDataEvent::IsMatch(const ov::String &amf_type)
{
	return (amf_type.UpperCaseString() == ov::String("onUserData").UpperCaseString() ||
			amf_type.UpperCaseString() == ov::String("onUserDataEvent").UpperCaseString());
}

std::shared_ptr<AmfUserDataEvent> AmfUserDataEvent::Create()
{
	return std::make_shared<AmfUserDataEvent>();
}

AmfUserDataEvent::AmfUserDataEvent()
{
	_type		 = "onUserDataEvent";
	_arrays		 = nullptr;
	_single_data = nullptr;
}

std::shared_ptr<AmfUserDataEvent> AmfUserDataEvent::Parse(const std::shared_ptr<ov::Data> &data)
{
	if (data == nullptr)
	{
		return nullptr;
	}

	auto object = std::make_shared<AmfUserDataEvent>();

	// Not implemented

	return object;
}

bool AmfUserDataEvent::IsValid(const Json::Value &json)
{
	if (json["data"].isNull())
	{
		return false;
	}

	return true;
}

std::shared_ptr<AmfUserDataEvent> AmfUserDataEvent::Parse(const Json::Value &json)
{
	if (json["data"].isNull())
	{
		return nullptr;
	}

	auto object = std::make_shared<AmfUserDataEvent>();

	auto data	= json["data"];
	if (data.isObject())
	{
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
	}
	else if (data.isBool())
	{
		object->SetData(data.asBool());
	}
	else if (data.isInt() || data.isUInt() || data.isDouble())
	{
		object->SetData(data.asDouble());
	}
	else if (data.isString())
	{
		object->SetData(data.asString().c_str());
	}

	ov::String amf_type = json["amfType"].asString().c_str();
	object->SetType(amf_type);

	return object;
}

bool AmfUserDataEvent::IsValid(const pugi::xml_node &xml)
{
	ov::String amf_type = xml.child_value("AmfType");
	if (!(amf_type.UpperCaseString() == "ONUSERDATA" || amf_type.UpperCaseString() == "ONUSERDATAEVENT"))
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

std::shared_ptr<AmfUserDataEvent> AmfUserDataEvent::Parse(const pugi::xml_node &xml)
{
	auto object = std::make_shared<AmfUserDataEvent>();
	if (object == nullptr)
	{
		return nullptr;
	}

	ov::String amf_type = xml.child_value("AmfType");
	object->SetType(amf_type);

	auto data_node = xml.child("Data");
	if (data_node.empty())
	{
		// logte("AMF event must have Data node");
		return nullptr;
	}

	size_t child_node_count = std::distance(data_node.begin(), data_node.end());
	if (child_node_count > 1)
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
	else
	{
		// Simple data
		ov::String type = data_node.attribute("type").as_string();
		if (type.IsEmpty())
		{
			type = "string";
		}
		ov::String value = data_node.child_value();

		if (type == "double" || type == "number" || type == "float")
		{
			double double_value = atof(value.CStr());
			object->SetData(double_value);
		}
		else if (type == "boolean" || type == "bool")
		{
			bool bool_value = (strcmp(value.CStr(), "true") == 0);
			object->SetData(bool_value);
		}
		else
		{
			object->SetData(value.CStr());
		}
	}

	return object;
}

std::shared_ptr<ov::Data> AmfUserDataEvent::Serialize() const
{
	AmfDocument doc;
	doc.AppendProperty(AmfProperty(_type.CStr()));

	if (_single_data != nullptr)
	{
		if (_single_data->GetType() == AmfTypeMarker::String)
		{
			ov::String str_value = _single_data->GetString();
			str_value			 = str_value.Replace("${EpochTime}", ov::String::FormatString("%" PRId64, ov::Time::GetTimestampInMs()));

			AmfProperty single_data(str_value.CStr());
			doc.AppendProperty(single_data);
		}
		else
		{
			doc.AppendProperty(*_single_data.get());
		}
	}

	if (_arrays != nullptr)
	{
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

		doc.AppendProperty(*arrays.get());
	}

	ov::ByteStream byte_stream;
	if (doc.Encode(byte_stream) == false)
	{
		return nullptr;
	}

	return byte_stream.GetDataPointer();
}