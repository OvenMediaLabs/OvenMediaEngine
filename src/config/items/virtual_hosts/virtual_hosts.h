//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "virtual_host.h"

namespace cfg
{
	namespace vhost
	{
		struct VirtualHosts : public Item
		{
		protected:
			std::vector<VirtualHost> _virtual_host_list;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetVirtualHostList, _virtual_host_list)

		protected:
			void MakeList() override
			{
				Register<OmitJsonName>("VirtualHost", &_virtual_host_list);
			}
		};
	}  // namespace vhost
}  // namespace cfg
