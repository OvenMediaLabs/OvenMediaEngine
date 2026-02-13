//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./amf_property.h"

namespace modules::rtmp
{
	struct AmfPropertyPair
	{
		ov::String name;
		AmfProperty property;

		AmfPropertyPair(const ov::String &name)
			: name(name),
			  property(AmfTypeMarker::Null)
		{
		}

		template <typename T>
		AmfPropertyPair(const ov::String &name, T &value)
			: name(name),
			  property(value)
		{
		}

		AmfPropertyPair(const ov::String &name, AmfTypeMarker type)
			: name(name),
			  property(type)
		{
		}

		bool IsTypeOf(AmfTypeMarker type) const
		{
			return (property.GetType() == type);
		}
	};
}  // namespace modules::rtmp
