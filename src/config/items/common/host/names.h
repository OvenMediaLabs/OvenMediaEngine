//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace cmn
	{
		struct Names : public Item
		{
		protected:
			std::vector<ov::String> _name_list;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetNameList, _name_list);

		protected:
			void MakeList() override
			{
				Register<OmitJsonName>("Name", &_name_list);
			}
		};
	}  // namespace cmn
}  // namespace cfg
