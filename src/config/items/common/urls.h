//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace cmn
	{
		struct Urls : public Item
		{
		protected:
			std::vector<ov::String> _url_list;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetUrlList, _url_list)

		protected:
			void MakeList() override
			{
				Register("Url", &_url_list);
			}
		};
	}  // namespace cmn
}  // namespace cfg