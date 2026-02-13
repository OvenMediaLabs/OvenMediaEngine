//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "publisher.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct SrtPublisher : public Publisher
				{
				public:
					PublisherType GetType() const override
					{
						return PublisherType::Srt;
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
