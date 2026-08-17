//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "origin.h"
#include "properties.h"

namespace cfg
{
	namespace vhost
	{
		namespace orgn
		{
			struct Origins : public Item
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetOriginList, _origin_list)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetProperties, _properties)

			protected:
				void MakeList() override
				{
					Register<Optional>("Origin", &_origin_list);
					Register<Optional>("Properties", &_properties);
				}

				std::vector<Origin> _origin_list;
				Properties _properties;
			};
		}  // namespace orgn
	}  // namespace vhost
}  // namespace cfg