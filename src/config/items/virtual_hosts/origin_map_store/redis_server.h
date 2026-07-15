//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../common/urls.h"

namespace cfg
{
	namespace vhost
	{
		namespace orgn
		{
			struct RedisServer : public Item
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetHost, _host)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetAuth, _auth)

			protected:
				void MakeList() override
				{
					Register("Host", &_host, nullptr,
							 [=]() -> std::shared_ptr<ConfigError> {
								 // format = host:port
								 auto tokens = _host.Split(":");
								 if (tokens.size() != 2)
								 {
									 return CreateConfigErrorPtr("Invalid host format %s > it must be host:port", _host.CStr());
								 }
								 return nullptr;
							 });
					Register<Optional>("Auth", &_auth);
				}

				ov::String _host;
				ov::String _auth;
			};
		}  // namespace orgn
	}  // namespace vhost
}  // namespace cfg