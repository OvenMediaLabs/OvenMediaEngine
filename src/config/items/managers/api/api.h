//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../common/cross_domain_support.h"
#include "storage/storage.h"

namespace cfg
{
	namespace mgr
	{
		namespace api
		{
			struct API : public Item, public cmn::CrossDomainSupport
			{
			protected:
				ov::String _access_token;

				Storage _storage;

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(GetAccessToken, _access_token)

				CFG_DECLARE_CONST_REF_GETTER_OF(GetStorage, _storage)

			protected:
				void MakeList() override
				{
					Register("AccessToken", &_access_token);

					Register<Optional>("Storage", &_storage);

					Register<Optional>("CrossDomains", &_cross_domains);
				}
			};
		}  // namespace api
	}  // namespace mgr
}  // namespace cfg
