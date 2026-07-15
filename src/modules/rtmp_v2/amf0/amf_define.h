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

namespace modules::rtmp
{
	enum class AmfTypeMarker : int32_t
	{
		Number = 0,
		Boolean,
		String,
		Object,
		MovieClip,
		Null,
		Undefined,
		Reference,
		EcmaArray,
		ObjectEnd,
		StrictArray,
		Date,
		LongString,
		Unsupported,
		Recordset,
		Xml,
		TypedObject,
	};
	constexpr const char *EnumToString(AmfTypeMarker type)
	{
		switch (type)
		{
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Number);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Boolean);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, String);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Object);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, MovieClip);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Null);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Undefined);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Reference);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, EcmaArray);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, ObjectEnd);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, StrictArray);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Date);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, LongString);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Unsupported);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Recordset);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, Xml);
			OV_CASE_RETURN_ENUM_STRING(AmfTypeMarker, TypedObject);
		}

		return "Unknown";
	}
}  // namespace modules::rtmp
