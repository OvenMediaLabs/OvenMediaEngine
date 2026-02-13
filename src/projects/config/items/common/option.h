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
		struct Option : public Item
		{
		protected:
			ov::String _key;
			ov::String _value;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetKey, _key)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetValue, _value)

		protected:
			void MakeList() override
			{
				Register("Key", &_key);
				Register("Value", &_value);
			}
		};
	}  // namespace cmn
}  // namespace cfg
