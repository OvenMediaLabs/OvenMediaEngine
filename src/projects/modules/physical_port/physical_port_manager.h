//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <memory>

#include "physical_port.h"
#include "physical_port_observer.h"

#define PHYSICAL_PORT_USE_DEFAULT_COUNT -1
#define PHYSICAL_PORT_DEFAULT_WORKER_COUNT 1

class PhysicalPortManager : public ov::Singleton<PhysicalPortManager>
{
public:
	friend class ov::Singleton<PhysicalPortManager>;

	virtual ~PhysicalPortManager();

	// name is up to 9 characters including null
	std::shared_ptr<PhysicalPort> CreatePort(const char *name,
											 ov::SocketType type,
											 const ov::SocketAddress &address,
											 int worker_count = PHYSICAL_PORT_USE_DEFAULT_COUNT,
											 bool thread_per_socket = false, // if true, worker_count is ignored
											 int send_buffer_size = 0,
											 int recv_buffer_size = 0,
											 const PhysicalPort::OnSocketCreated on_socket_created = nullptr);

	bool DeletePort(std::shared_ptr<PhysicalPort> &port);

protected:
	PhysicalPortManager();

	std::shared_ptr<ov::SocketPool> GetSocketPool(const std::pair<ov::SocketType, ov::SocketAddress> &socket_info, int socket_pool_count);

	std::map<std::pair<ov::SocketType, ov::SocketAddress>, std::shared_ptr<PhysicalPort>> _port_list;
	std::map<std::pair<ov::SocketType, ov::SocketAddress>, std::shared_ptr<ov::SocketPool>> _socket_pool_list;

	std::mutex _port_list_mutex;
};
