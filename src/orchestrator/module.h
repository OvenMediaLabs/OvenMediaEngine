//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "interfaces.h"

namespace ocst
{
	class Module
	{
	public:
		Module(ModuleType type, const std::shared_ptr<ModuleInterface> &module);

		ModuleType GetType() const;
		std::shared_ptr<ModuleInterface> GetModuleInterface() const;

		// Get Module as a specific type
		template <typename T>
		std::shared_ptr<T> GetModuleAs() const
		{
			return std::dynamic_pointer_cast<T>(_module);
		}

	private:
		ModuleType _type = ModuleType::Unknown;
		std::shared_ptr<ModuleInterface> _module = nullptr;
	};

} // namespace ocst