//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./managers/managers.h"
#include "./providers/providers.h"
#include "./publishers/publishers.h"

namespace cfg
{
	namespace bind
	{
		struct Bind : public Item
		{
		protected:
			mgr::Managers _managers;
			pvd::Providers _providers;
			pub::Publishers _publishers;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetManagers, _managers)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetProviders, _providers)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetPublishers, _publishers)

		protected:
			void MakeList() override
			{
				Register<Optional>("Managers", &_managers);
				Register<Optional>("Providers", &_providers);
				Register<Optional>("Publishers", &_publishers);
			}
		};
	}  // namespace bind
}  // namespace cfg
