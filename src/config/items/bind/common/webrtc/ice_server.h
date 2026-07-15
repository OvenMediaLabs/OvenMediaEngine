//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../common/urls.h"

namespace cfg
{
	namespace bind
	{
		namespace cmm
		{
			struct IceServer : public Item
			{
			protected:
				cmn::Urls _urls;
				ov::String _user_name;
				ov::String _credential;

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(GetUrls, _urls)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetUserName, _user_name)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetCredential, _credential)

			protected:
				void MakeList() override
				{
					Register("Urls", &_urls);
					Register<Optional>("UserName", &_user_name);
					Register<Optional>("Credential", &_credential);
				}
			};
		}  // namespace cmm
	}  // namespace bind
}  // namespace cfg
