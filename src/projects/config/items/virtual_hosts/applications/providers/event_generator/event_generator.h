//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "event.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct EventGenerator : public Item
				{
				protected:
					std::vector<Event> _events;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetEvents, _events);

				protected:
					void MakeList() override
					{
						Register<Optional>("Event", &_events);
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
