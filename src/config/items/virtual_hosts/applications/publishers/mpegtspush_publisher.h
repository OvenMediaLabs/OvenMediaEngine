//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../common/cross_domain_support.h"
#include "publisher.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct MpegtsPushPublisher : public Publisher
				{
				public:
					PublisherType GetType() const override
					{
						return PublisherType::MpegtsPush;
					}

				protected:
					void MakeList() override
					{
						Publisher::MakeList();
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
