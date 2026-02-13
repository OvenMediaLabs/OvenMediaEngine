//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "provider.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct OvtProvider : public Provider
				{
					ProviderType GetType() const override
					{
						return ProviderType::Ovt;
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg