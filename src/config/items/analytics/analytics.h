//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "forwarding.h"

namespace cfg
{
	namespace an
	{
		struct Analytics : public Item
		{
		protected:
			ov::String _user_key;
			Forwarding _forwarding;
		
		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetUserKey, _user_key)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetForwarding, _forwarding)

		protected:
			void MakeList() override
			{
				Register<Optional>("UserKey", &_user_key);
				Register<Optional>("Forwarding", &_forwarding);
			}
		};
	}  // namespace p2p
}  // namespace cfg