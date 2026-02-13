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
				bool _tcp_force = false;

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(GetIceCandidateList, _ice_candidate_list);
				CFG_DECLARE_CONST_REF_GETTER_OF(GetTcpRelayList, _tcp_relay_list);

				CFG_DECLARE_CONST_REF_GETTER_OF(GetEnableLinkLocalAddress, _enable_link_local_address)

				CFG_DECLARE_CONST_REF_GETTER_OF(GetTcpRelayWorkerCount, _tcp_relay_worker_count);
				CFG_DECLARE_CONST_REF_GETTER_OF(GetIceWorkerCount, _ice_worker_count);
				CFG_DECLARE_CONST_REF_GETTER_OF(IsTcpForce, _tcp_force)

			protected:
				void MakeList() override
				{
					Register<Optional>("IceCandidate", &_ice_candidate_list);
					Register<Optional>("TcpRelay", &_tcp_relay_list);

					Register<Optional>("EnableLinkLocalAddress", &_enable_link_local_address);

					Register<Optional>("TcpRelayWorkerCount", &_tcp_relay_worker_count);
					Register<Optional>("IceWorkerCount", &_ice_worker_count);
					Register<Optional>("TcpForce", &_tcp_force);
				}
			};
		}  // namespace cmm
	}  // namespace bind
}  // namespace cfg
