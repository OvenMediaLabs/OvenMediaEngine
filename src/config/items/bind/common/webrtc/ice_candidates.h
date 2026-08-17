//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace bind
	{
		namespace cmm
		{
			struct IceCandidates : public Item
			{
			protected:
				std::vector<ov::String> _ice_candidate_list{"*:10000/udp"};
				std::vector<ov::String> _tcp_relay_list;

				bool _enable_link_local_address = false;

				int _tcp_relay_worker_count{};
				int _ice_worker_count{};
				int _tcp_ice_worker_count{};
				bool _tcp_relay_force = false;
				ov::String _default_transport = "UDPTCP";

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetIceCandidateList, _ice_candidate_list);
			CFG_DECLARE_CONST_REF_GETTER_OF(GetTcpRelayList, _tcp_relay_list);

			CFG_DECLARE_CONST_REF_GETTER_OF(GetEnableLinkLocalAddress, _enable_link_local_address)

			CFG_DECLARE_CONST_REF_GETTER_OF(GetTcpRelayWorkerCount, _tcp_relay_worker_count);
			CFG_DECLARE_CONST_REF_GETTER_OF(GetIceWorkerCount, _ice_worker_count);
			CFG_DECLARE_CONST_REF_GETTER_OF(GetTcpIceWorkerCount, _tcp_ice_worker_count);
			CFG_DECLARE_CONST_REF_GETTER_OF(IsTcpRelayForce, _tcp_relay_force)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetDefaultTransport, _default_transport)
			protected:
				void MakeList() override
				{
					Register<Optional>("IceCandidate", &_ice_candidate_list);
					Register<Optional>("TcpRelay", &_tcp_relay_list);

					Register<Optional>("EnableLinkLocalAddress", &_enable_link_local_address);

					Register<Optional>("TcpRelayWorkerCount", &_tcp_relay_worker_count);
					Register<Optional>("IceWorkerCount", &_ice_worker_count);
					Register<Optional>("TcpIceWorkerCount", &_tcp_ice_worker_count);
					Register<Optional>("TcpRelayForce", &_tcp_relay_force);
				Register<Optional>("DefaultTransport", &_default_transport, nullptr,
								   [=]() -> std::shared_ptr<ConfigError> {
									   auto upper = _default_transport.UpperCaseString();
									   if (upper != "UDP" && upper != "TCP" && upper != "RELAY" && upper != "UDPTCP" && upper != "ALL")
									   {
										   logw("Config", "Invalid <DefaultTransport> value: '%s'. Falling back to 'UDPTCP'. Valid values: udp, tcp, relay, udptcp, all", _default_transport.CStr());
										   _default_transport = "UDPTCP";
									   }
									   else
									   {
										   _default_transport = upper;
									   }
									   return nullptr;
								   });
				Register<Optional>("TcpForce", &_tcp_relay_force, nullptr,
									   [=]() -> std::shared_ptr<ConfigError> {
										   logw("Config", "TcpForce is deprecated. Use TcpRelayForce instead.");
										   return nullptr;
									   });
				}
			};
		}  // namespace cmm
	}  // namespace bind
}  // namespace cfg
