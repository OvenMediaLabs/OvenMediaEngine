//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2023 AirenSoft. All rights reserved.
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
				struct SrtPushPublisher : public Publisher
				{
				public:
					PublisherType GetType() const override
					{
						return PublisherType::SrtPush;
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