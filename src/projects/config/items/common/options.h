//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace cmn
	{
		struct Options : public Item
		{
		protected:
			std::vector<Option> _option_list;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetOptionList, _option_list)

		protected:
			void MakeList() override
			{
				Register<OmitJsonName>("Option", &_option_list);
			}
		};
	}  // namespace cmn
}  // namespace cfg
