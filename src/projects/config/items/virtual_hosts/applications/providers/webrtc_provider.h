//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../common/cross_domain_support.h"
#include "provider.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct WebrtcProvider : public Provider, public cmn::CrossDomainSupport
				{
					ProviderType GetType() const override
					{
						return ProviderType::WebRTC;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetTimeout, _timeout)

				protected:
					void MakeList() override
					{
						Provider::MakeList();

						Register<Optional>("Timeout", &_timeout);
						Register<Optional>("CrossDomains", &_cross_domains);
					}

					int _timeout = 30000;
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg