//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../common/urls.h"

namespace cfg
{
	namespace vhost
	{
		namespace orgn
		{
			struct Pass : public Item
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetScheme, _scheme)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetUrlList, _urls.GetUrlList())
				CFG_DECLARE_CONST_REF_GETTER_OF(GetUrls, _urls)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsForwardQueryParamsEnabled, _forward_query_params)

			protected:
				void MakeList() override
				{
					Register("Scheme", &_scheme);
					Register("Urls", &_urls);
					Register<Optional>("ForwardQueryParams", &_forward_query_params);
				}

				ov::String _scheme;
				cmn::Urls _urls;
				bool _forward_query_params = true;
			};
		}  // namespace orgn
	}  // namespace vhost
}  // namespace cfg