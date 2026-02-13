//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "ice_port_manager.h"

#include <modules/address/address_utilities.h>
#include <modules/rtc_signalling/rtc_ice_candidate.h>

#include "ice_private.h"

std::shared_ptr<IcePort> IcePortManager::CreatePort(std::shared_ptr<IcePortObserver> observer)
{
	if (_ice_port == nullptr)
	{
		_ice_port = std::make_shared<IcePort>();
	}

	auto new_id = _last_issued_observer_id++;

	observer->SetId(new_id);
	_registered_observer_map[new_id] = observer;

	return _ice_port;
}

bool IcePortManager::IsRegisteredObserver(const std::shared_ptr<IcePortObserver> &observer)
{
	for (const auto &registered_observer : _registered_observer_map)
	{
		if (registered_observer.first == observer->GetId())
		{
			return true;
		}
	}

	return false;
}

bool IcePortManager::CreateIceCandidates(
	const char *server_name,
	std::shared_ptr<IcePortObserver> observer,
	const cfg::Server &server_config,
	const cfg::bind::cmm::IceCandidates &ice_candidates_config)
{
	if ((_ice_port == nullptr) || (IsRegisteredObserver(observer) == false))
	{
		logte("ICE port should be created before creating ICE candidates");
		return false;
	}

	RtcIceCandidateList ice_candidate_list;

	if (GenerateIceCandidates(ice_candidates_config, &ice_candidate_list) == false)
	{
		logte("Could not parse ICE candidates for %s. Check your ICE configuration", server_name);
		return false;
	}

	bool is_parsed;
	auto ice_worker_count = ice_candidates_config.GetIceWorkerCount(&is_parsed);
	ice_worker_count	  = is_parsed ? ice_worker_count : PHYSICAL_PORT_USE_DEFAULT_COUNT;

	if (_ice_port->CreateIceCandidates(server_name, server_config, ice_candidate_list, ice_worker_count) == false)
	{
		Release(observer);

		logte("Could not create ICE candidates for %s. Check your ICE configuration", server_name);
		OV_ASSERT2(false);
	}
	else
	{
		observer->AppendIceCandidates(ice_candidate_list);
		logtt("IceCandidates are created successfully: %s", _ice_port->ToString().CStr());
	}

	return true;
}

bool IcePortManager::CreateTurnServer(std::shared_ptr<IcePortObserver> observer, const ov::SocketAddress &listening, const ov::SocketType socket_type, int tcp_relay_worker_count)
{
	if (_ice_port == nullptr || IsRegisteredObserver(observer) == false)
	{
		return false;
	}

	if (_ice_port->CreateTurnServer(listening, socket_type, tcp_relay_worker_count) == false)
	{
		Release(observer);
		logte("Could not create TURN server");

		return false;
	}
	else
	{
		observer->SetTurnServerSocketType(socket_type);
		observer->SetTurnServerPort(listening.Port());
	}

	return true;
}

bool IcePortManager::CreateTurnServersInternal(
	const char *server_name,
	const std::shared_ptr<IcePort> &ice_port,
	const std::shared_ptr<IcePortObserver> &observer,
	const cfg::Server &server_config, const cfg::bind::cmm::Webrtc &webrtc_bind_config)
{
	auto &ice_candidates_config	 = webrtc_bind_config.GetIceCandidates();

	bool is_tcp_relay_configured = false;
	const auto &tcp_relay_list	 = ice_candidates_config.GetTcpRelayList(&is_tcp_relay_configured);
	std::vector<ov::String> tcp_relay_address_string_list;

	if (is_tcp_relay_configured)
	{
		bool is_tcp_relay_worker_count_configured;
		auto tcp_relay_worker_count = ice_candidates_config.GetTcpRelayWorkerCount(&is_tcp_relay_worker_count_configured);
		tcp_relay_worker_count		= is_tcp_relay_worker_count_configured ? tcp_relay_worker_count : PHYSICAL_PORT_USE_DEFAULT_COUNT;

		auto &ip_list				= server_config.GetIPList();

		if (ip_list.empty())
		{
			logtw("No IP is configured");
			return false;
		}

		std::map<ov::SocketAddress, bool> bound_map;

		for (auto &tcp_relay : tcp_relay_list)
		{
			const auto tcp_relay_address = ov::SocketAddress::ParseAddress(tcp_relay);

			if (tcp_relay_address.HasPortList() == false)
			{
				logte("Invalid TCP relay address of %s: %s (The TCP relay address must be in <IP>:<Port> format)",
					  server_name,
					  tcp_relay.CStr());
				return false;
			}

			if (tcp_relay_address.EachPort(
					[&](const ov::String &host, const uint16_t port) -> bool {
						std::vector<ov::SocketAddress> tcp_relay_address_list;

						try
						{
							tcp_relay_address_list = ov::SocketAddress::Create(ip_list, port);
						}
						catch (ov::Error &e)
						{
							logte("Could not get address for port: %d (%s)", port, server_name);
							return false;
						}

						for (auto &address : tcp_relay_address_list)
						{
							auto found			= bound_map.find(address);
							auto address_string = ov::String::FormatString("%s/%s", address.ToString().CStr(), ov::StringFromSocketType(ov::SocketType::Tcp));

							if (found != bound_map.end())
							{
								logtt("TURN port is already bound to %s", address_string.CStr());
								continue;
							}

							bound_map.emplace(address, true);

							if (CreateTurnServer(observer, address, ov::SocketType::Tcp, tcp_relay_worker_count) == false)
							{
								logte("Could not create TURN Server on %s for %s. Check your configuration", address_string.CStr(), server_name);
								return false;
							}

							logtt("TURN port is bound to %s", address_string.CStr());

							tcp_relay_address_string_list.emplace_back(address_string);
						}

						return true;
					}) == false)
			{
				return false;
			}
		}
	}

	if (tcp_relay_address_string_list.empty())
	{
		logti("No TURN server is configured for %s", server_name);
	}
	else
	{
		logti("TURN port%s listening on %s (for %s)",
			  (tcp_relay_address_string_list.size() == 1) ? " is" : "s are",
			  ov::String::Join(tcp_relay_address_string_list, ", ").CStr(),
			  server_name);
	}

	return true;
}

std::shared_ptr<IcePort> IcePortManager::CreateTurnServers(
	const char *server_name,
	std::shared_ptr<IcePortObserver> observer,
	const cfg::Server &server_config, const cfg::bind::cmm::Webrtc &webrtc_bind_config)
{
	std::shared_ptr<IcePort> ice_port = CreatePort(observer);

	if (ice_port == nullptr)
	{
		logte("Could not initialize ICE port. Check your ICE configuration");
		return nullptr;
	}

	auto &ice_candidates_config = webrtc_bind_config.GetIceCandidates();

	if (
		CreateIceCandidates(server_name, observer, server_config, ice_candidates_config) &&
		CreateTurnServersInternal(server_name, ice_port, observer, server_config, webrtc_bind_config))
	{
		return ice_port;
	}

	ice_port->Close();

	// TODO: Need to do something for unsubscribed observer
	return nullptr;
}

bool IcePortManager::Release(std::shared_ptr<IcePortObserver> observer)
{
	if (IsRegisteredObserver(observer) == false)
	{
		return false;
	}

	if (_ice_port != nullptr)
	{
		_ice_port->Close();
		_ice_port = nullptr;
	}

	_registered_observer_map.clear();

	return true;
}

bool IcePortManager::GenerateIceCandidates(const cfg::bind::cmm::IceCandidates &ice_candidates_config, RtcIceCandidateList *ice_candidate_list)
{
	auto &list_config = ice_candidates_config.GetIceCandidateList();

	std::unordered_map<
		// key: port
		int,
		std::unordered_map<
			// key: socket_type
			ov::SocketType,
			std::unordered_map<
				// key: address
				ov::SocketAddress,
				// value: always true
				bool>>>
		port_map;

	const auto mapped_address_list = ov::AddressUtilities::GetInstance()->GetMappedAddressList();

	for (auto &ice_candidate_config : list_config)
	{
		std::vector<ov::String> ip_list;
		ov::SocketType socket_type;
		ov::SocketAddress::Address address;

		if (ParseIceCandidate(ice_candidate_config, &ip_list, &socket_type, &address, ice_candidates_config.GetEnableLinkLocalAddress()) == false)
		{
			return false;
		}

		const ov::String protocol = StringFromSocketType(socket_type);

		address.EachPort([&](const ov::String &host, const uint16_t port) -> bool {
			auto &socket_type_map = port_map[port];
			auto &address_map	  = socket_type_map[socket_type];

			std::vector<ov::SocketAddress> address_list;

			for (const auto &local_ip : ip_list)
			{
				try
				{
					address_list = ov::SocketAddress::Create(local_ip, static_cast<uint16_t>(port));
				}
				catch (const ov::Error &e)
				{
					logtw("Invalid address: %s, port: %d - %s",
						  local_ip.CStr(), port, e.What());
					return false;
				}

				for (const auto &address : address_list)
				{
					if (address_map.find(address) != address_map.end())
					{
						// Already exists
						logti("Duplicated ICE candidate found: %s", address.ToString().CStr());
						continue;
					}

					logti("ICE candidate found: %s", address.ToString().CStr());
					address_map.emplace(address, true);
				}
			}
			return true;
		});
	}

	// Create an ICE candidate using port_map (group by port number)
	for (auto &port_item : port_map)
	{
		std::vector<RtcIceCandidate> ice_candidates;

		for (auto &socket_type_item : port_item.second)
		{
			const auto socket_type	  = socket_type_item.first;
			const auto &address_map	  = socket_type_item.second;

			const ov::String protocol = StringFromSocketType(socket_type);

			for (auto &address_map_item : address_map)
			{
				ice_candidates.emplace_back(protocol, address_map_item.first, 0, "");
			}
		}

		ice_candidate_list->push_back(std::move(ice_candidates));
	}

	return true;
}

bool IcePortManager::ParseIceCandidate(const ov::String &ice_candidate, std::vector<ov::String> *ip_list, ov::SocketType *socket_type, ov::SocketAddress::Address *address, const bool include_local_link_address)
{
	// Create SocketAddress instances from string
	//
	// Expression examples:
	//
	// - Case #1
	//   - Expression: 192.168.0.1:10000
	//   - Meaning: IP = 192.168.0.1, Port = 10000, Protocol: TCP
	//   - It generates a instance:
	//     [0] 192.168.0.1, 10000/TCP
	// - Case #2
	//   - Expression: 192.168.0.1:10000/udp
	//   - Meaning: IP = 192.168.0.1, Port = 10000, Protocol: UDP
	//   - It generates a instance:
	//     [0] 192.168.0.1, 10000/UDP
	// - Case #3
	//   - Expression: *:10000/udp
	//   - Meaning: IP = <All IPv4 addresses>, Port = 10000, Protocol: UDP
	//   - It may generates multiple instances:
	//     [0] 192.168.0.1, 10000/UDP
	//     [1] 10.0.0.1, 10000/UDP
	// - Case #4
	//   - Expression: *:10000-10002/udp
	//   - Meaning: IP = <All IPv4 addresses>, Port = Total 3 ports (10000 to 10002), Protocol: UDP
	//   - It may generates multiple instances:
	//     [0] 192.168.0.1, 10000/UDP
	//     [1] 192.168.0.1, 10001/UDP
	//     [2] 192.168.0.1, 10002/UDP
	//     [3] 10.0.0.1, 10000/UDP
	//     [4] 10.0.0.1, 10001/UDP
	//     [5] 10.0.0.1, 10002/UDP
	// - Case #5
	//   - Expression: [::]:10000/udp
	//   - Meaning: IP = <All IPv6 addresses>, Port = 10000, Protocol: UDP
	//   - It may generates multiple instances:
	//     [0] fe80::1, 10000/UDP
	//     [1] fe80::2, 10000/UDP

	// <IP>:<Port range>[/<Protocol>]
	// Split by "/" to parse IP and Port range
	auto tokens = ice_candidate.Split("/");

	if (tokens.size() > 2)
	{
		logte("Invalid ICE candidate in configuration: %s", ice_candidate.CStr());
		return false;
	}

	auto protocol		= (tokens.size() == 2) ? tokens[1].UpperCaseString() : "";

	ov::SocketType type = ov::SocketType::Tcp;
	if (protocol == "UDP")
	{
		type = ov::SocketType::Udp;
	}
	else if (protocol == "TCP")
	{
		type = ov::SocketType::Tcp;
	}
	else if (protocol.IsEmpty() == false)
	{
		logte("Invalid ICE candidate found - not supported protocol: %s", tokens[1].CStr());
		return false;
	}
	else
	{
		// Use default value
	}

	*address			   = ov::SocketAddress::ParseAddress(tokens[0]);
	auto address_utilities = ov::AddressUtilities::GetInstance();

	const auto &host	   = address->host;

	if (host.IsEmpty())
	{
		// Port only specified - Add both of IPv4 and IPv6 addresses
		auto mapped_address_list = address_utilities->GetIPv4List();
		ip_list->insert(ip_list->end(), mapped_address_list.cbegin(), mapped_address_list.cend());
		mapped_address_list = address_utilities->GetIPv6List(include_local_link_address);
		ip_list->insert(ip_list->end(), mapped_address_list.cbegin(), mapped_address_list.cend());
	}
	else if (host == OV_SOCKET_WILDCARD_IPV4)
	{
		// IPv4 wildcard - Add IPv4 addresses
		auto local_ip_list = address_utilities->GetIPv4List();
		ip_list->insert(ip_list->end(), local_ip_list.cbegin(), local_ip_list.cend());
	}
	else if (host == OV_SOCKET_WILDCARD_IPV6)
	{
		// IPv6 wildcard - Add IPv6 addresses
		auto local_ip_list = address_utilities->GetIPv6List(include_local_link_address);
		ip_list->insert(ip_list->end(), local_ip_list.cbegin(), local_ip_list.cend());
	}
	else if (host == OV_ICE_PORT_PUBLIC_IP)
	{
		// Public IP - Add public IP address
		auto public_ip_list = address_utilities->GetMappedAddressList();

		if (public_ip_list.empty() == false)
		{
			for (auto &public_ip : public_ip_list)
			{
				ip_list->emplace_back(public_ip.GetIpAddress());
			}
		}
		else
		{
			logtw(OV_ICE_PORT_PUBLIC_IP " is specified on ICE candidate, but failed to obtain public IP: %s", ice_candidate.CStr());
		}
	}
	else
	{
		ip_list->push_back(host);
	}

	*socket_type = type;

	return true;
}