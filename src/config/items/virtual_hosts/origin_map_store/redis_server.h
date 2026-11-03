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
				CFG_DECLARE_CONST_REF_GETTER_OF(IsClusterEnabled, _cluster)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsTlsEnabled, _tls)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetCaCert, _ca_cert)

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
					Register<Optional>("Cluster", &_cluster);
					Register<Optional>("Tls", &_tls);
					Register<Optional>("CaCert", &_ca_cert);
				}

				ov::String _host;
				ov::String _auth;
				bool _cluster = false;
				bool _tls = false;
				ov::String _ca_cert;
			};
		}  // namespace orgn
	}  // namespace vhost
}  // namespace cfg