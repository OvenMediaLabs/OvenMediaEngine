//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

#include <vector>

#include "./amf_strict_array.h"

namespace modules::rtmp
{
	class AmfDocument
	{
	public:
		bool Encode(ov::ByteStream &byte_stream) const;
		bool Decode(ov::ByteStream &byte_stream);

		bool Append(const AmfProperty &property);

		size_t GetCount() const
		{
			return _amf_properties.size();
		}

		AmfProperty *Get(size_t index);
		const AmfProperty *Get(size_t index) const;
		const AmfProperty *Get(size_t index, AmfTypeMarker expected_type) const;

		const AmfProperty *GetObject(size_t index) const;
		std::optional<ov::String> GetString(size_t index) const;
		std::optional<double> GetNumber(size_t index) const;

		ov::String ToString(int indent = 0) const;

	private:
		std::vector<AmfProperty> _amf_properties;
	};

	class AmfDocumentBuilder
	{
	public:
		AmfDocumentBuilder &Append(const AmfProperty &property)
		{
			_amf_document.Append(property);
			return *this;
		}

		AmfDocument Build()
		{
			return std::move(_amf_document);
		}

	private:
		AmfDocument _amf_document;
	};
}  // namespace modules::rtmp
