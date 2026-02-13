//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace ocst
{
	enum class ModuleType
	{
		Unknown = 0x10000000,
		PushProvider = 0x00000001,
		PullProvider = 0x00000002,
		MediaRouter = 0x00000010,
		Transcoder = 0x00000100,
		Publisher = 0x00001000
	};

	// Operators for ModuleType
	inline ModuleType operator|(ModuleType type1, ModuleType type2)
	{
		return static_cast<ModuleType>(
			ov::ToUnderlyingType(type1) |
			ov::ToUnderlyingType(type2));
	}

	inline ModuleType operator&(ModuleType type1, ModuleType type2)
	{
		return static_cast<ModuleType>(
			ov::ToUnderlyingType(type1) &
			ov::ToUnderlyingType(type2));
	}

	ov::String GetModuleTypeName(ModuleType type);

	enum class Result
	{
		// An error occurred
		Failed,
		// Created successfully
		Succeeded,
		// The item does exists
		Exists,
		// The item does not exists
		NotExists
	};

	enum class ItemState
	{
		Unknown,
		// This item is applied to OriginMap
		Applied,
		// Need to check if this item has changed
		NeedToCheck,
		// This item is applied, and not changed
		NotChanged,
		// This item is not applied, and will be applied to OriginMap/OriginList
		New,
		// This item is applied, but need to change some values
		Changed,
		// This item is applied, and will be deleted from OriginMap/OriginList
		Delete
	};
}  // namespace ocst
