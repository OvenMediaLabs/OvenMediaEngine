//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "rules/rules.h"

namespace cfg
{
	namespace alrt
	{
		struct Alert : public Item
		{
		protected:
			ov::String _url;
			ov::String _secret_key;
			int _timeout_msec;

			ov::String _rules_file;
			rule::Rules _rules;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetUrl, _url)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetSecretKey, _secret_key)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetTimeoutMsec, _timeout_msec)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetRulesFile, _rules_file)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetRules, _rules)

		protected:
			void MakeList() override
			{
				Register("Url", &_url);
				Register("SecretKey", &_secret_key);
				Register("Timeout", &_timeout_msec);
				Register<Optional, ResolvePath>("RulesFile", &_rules_file);
				Register<Optional>("Rules", &_rules);
			}
		};
	}  // namespace alrt
}  // namespace cfg
