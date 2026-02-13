//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "enabled_modules.h"

namespace cfg
{
	namespace vhost
	{
		namespace sig
		{
			struct Enables : public Item
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetProviders, _providers)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetPublishers, _publishers)

			protected:
				void MakeList() override
				{
					Register<Optional>("Providers", &_providers);
					Register<Optional>("Publishers", &_publishers);
				}

				EnabledModules _providers;
				EnabledModules _publishers;
			};
		}  // namespace sig
	}  // namespace vhost
}  // namespace cfg