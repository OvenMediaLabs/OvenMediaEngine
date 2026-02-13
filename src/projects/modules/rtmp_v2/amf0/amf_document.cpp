//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "amf_document.h"

#include "providers/rtmp/rtmp_provider_private.h"

namespace modules::rtmp
{
	bool AmfDocument::Encode(ov::ByteStream &byte_stream) const
	{
		for (auto &property : _amf_properties)
		{
			if (property.Encode(byte_stream, true) == false)
			{
				return false;
			}
		}

		return true;
	}

	bool AmfDocument::Decode(ov::ByteStream &byte_stream)
	{
		while (byte_stream.IsEmpty() == false)
		{
			AmfProperty property;
			if (property.Decode(byte_stream, true) == false)
			{
				return false;
			}

			_amf_properties.push_back(std::move(property));
		}

		return true;
	}

	bool AmfDocument::Append(const AmfProperty &property)
	{
		_amf_properties.push_back(property);

		return true;
	}

	AmfProperty *AmfDocument::Get(size_t index)
	{
		return (index < _amf_properties.size()) ? &(_amf_properties[index]) : nullptr;
	}

	const AmfProperty *AmfDocument::Get(size_t index) const
	{
		return (index < _amf_properties.size()) ? &(_amf_properties[index]) : nullptr;
	}

	const AmfProperty *AmfDocument::Get(size_t index, AmfTypeMarker expected_type) const
	{
		auto property = Get(index);

		if ((property != nullptr) && (property->GetType() == expected_type))
		{
			return property;
		}

		return nullptr;
	}

	const AmfProperty *AmfDocument::GetObject(size_t index) const
	{
		return Get(index, AmfTypeMarker::Object);
	}

	std::optional<ov::String> AmfDocument::GetString(size_t index) const
	{
		auto property = Get(index);

		if (property != nullptr)
		{
			return property->GetString();
		}

		return std::nullopt;
	}

	std::optional<double> AmfDocument::GetNumber(size_t index) const
	{
		auto property = Get(index);

		if (property != nullptr)
		{
			return property->GetNumber();
		}

		return std::nullopt;
	}

	ov::String AmfDocument::ToString(int indent) const
	{
		auto indent_string = ov::String::Repeat("    ", indent);
		auto item_indent_string = ov::String::Repeat("    ", indent + 1);

		ov::String description;
		size_t index = 0;

		description.AppendFormat("%s{  (AmfDocument)\n", indent_string.CStr());

		for (auto &property : _amf_properties)
		{
			description.AppendFormat("%s%zu: ", item_indent_string.CStr(), index++);
			property.ToString(description, indent + 1);
			description.Append('\n');
		}

		description.AppendFormat("%s}", indent_string.CStr());

		return description;
	}
}  // namespace modules::rtmp
