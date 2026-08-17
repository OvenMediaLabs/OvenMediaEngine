//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace cmn
	{
		struct UtcTiming : public Item
		{
		protected:
			ov::String _scheme = "urn:mpeg:dash:utc:http-xsdate:2014";
			ov::String _value  = "/time?iso&ms";

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetScheme, _scheme)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetValue, _value)

		protected:
			void MakeList() override
			{
				Register<Optional>("Scheme", &_scheme);
				Register<Optional>("Value", &_value);
			}
		};
	}  // namespace cmn
}  // namespace cfg