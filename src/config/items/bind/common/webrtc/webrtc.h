//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./ice_candidates.h"
#include "./ice_servers.h"
#include "./signalling.h"

namespace cfg
{
	namespace bind
	{
		namespace cmm
		{
			struct Webrtc : public Item
			{
			protected:
				Signalling _signalling;
				ov::String _tcp_relay_bind;
				IceCandidates _ice_candidates;
				IceServers _ice_servers;

			public:
				explicit Webrtc(const char *port)
					: _signalling(port)
				{
				}

				Webrtc(const char *port, const char *tls_port)
					: _signalling(port, tls_port)
				{
				}

				CFG_DECLARE_CONST_REF_GETTER_OF(GetSignalling, _signalling)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetTcpRelayBind, _tcp_relay_bind);
				CFG_DECLARE_CONST_REF_GETTER_OF(GetIceCandidates, _ice_candidates)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetIceServers, _ice_servers)

			protected:
				void MakeList() override
				{
					Register<Optional>("Signalling", &_signalling);
					Register<Optional>("TcpRelayBind", &_tcp_relay_bind);
					Register<Optional>("IceCandidates", &_ice_candidates);
					Register<Optional>("IceServers", &_ice_servers);
				};
			};
		}  // namespace cmm
	}  // namespace bind
}  // namespace cfg