//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "redis_server.h"

namespace cfg
{
	namespace vhost
	{
		namespace orgn
		{
			struct OriginMapStore : public Item
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetRedisServer, _redis_server)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetOriginHostName, _origin_host_name)

			protected:
				void MakeList() override
				{
					Register("RedisServer", &_redis_server);
					Register<Optional>("OriginHostName", &_origin_host_name);
				}
				
				RedisServer _redis_server;
				ov::String _origin_host_name;
			};
		}  // namespace orgn
	}	   // namespace vhost
}  // namespace cfg