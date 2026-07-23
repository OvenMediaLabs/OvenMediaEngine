//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "module.h"

namespace ocst
{
	Module::Module(ModuleType type, const std::shared_ptr<ModuleInterface> &module)
		: _type(type),
		  _module(module)
	{
	}

	ModuleType Module::GetType() const 
	{ 
		return _type; 
	}
	
	std::shared_ptr<ModuleInterface> Module::GetModuleInterface() const 
	{ 
		return _module; 
	}

} // namespace ocst