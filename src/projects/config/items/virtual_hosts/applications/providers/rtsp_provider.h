//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
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
				struct RtspProvider : public Provider
				{
					ProviderType GetType() const override
					{
						return ProviderType::Rtsp;
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg