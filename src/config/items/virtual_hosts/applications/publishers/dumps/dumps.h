//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "dump.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct Dumps : public Item
				{
				protected:
					std::vector<Dump> _dumps;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDumps, _dumps);

				protected:
					void MakeList() override
					{
						Register<Optional>("Dump", &_dumps);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg