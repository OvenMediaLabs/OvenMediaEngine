#pragma once

#include <stdint.h>

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