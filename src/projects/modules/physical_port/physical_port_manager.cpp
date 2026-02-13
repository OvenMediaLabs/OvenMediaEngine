//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "physical_port_manager.h"

#include "physical_port_private.h"

PhysicalPortManager::PhysicalPortManager()
{
}

PhysicalPortManager::~PhysicalPortManager()
{
}

std::shared_ptr<PhysicalPort> PhysicalPortManager::CreatePort(const char *name,
															  ov::SocketType type,
															  const ov::SocketAddress &address,
															  int worker_count,
															  bool thread_per_socket,
															  int send_buffer_size,
															  int recv_buffer_size,
															  const PhysicalPort::OnSocketCreated on_socket_created)
{
	auto lock_guard = std::lock_guard(_port_list_mutex);

	auto key = std::make_pair(type, address);
	auto item = _port_list.find(key);
	std::shared_ptr<PhysicalPort> port = nullptr;

	if (worker_count == PHYSICAL_PORT_USE_DEFAULT_COUNT)
	{
		worker_count = PHYSICAL_PORT_DEFAULT_WORKER_COUNT;
	}

	if (item == _port_list.end())
	{
		port = std::make_shared<PhysicalPort>(PhysicalPort::PrivateToken{nullptr});

		if (port->Create(name, type, address, worker_count, thread_per_socket, send_buffer_size, recv_buffer_size, on_socket_created))
		{
			_port_list[key] = port;
		}
		else
		{
			port = nullptr;
		}
	}
	else
	{
		port = item->second;
	}

	if (port != nullptr)
	{
		port->IncreaseRefCount();

		if (thread_per_socket == false && port->GetWorkerCount() != worker_count)
		{
			logtw("The number of workers in the existing socket pool differs from the number of workers passed by the argument: socket pool: %d, argument: %d",
				  port->GetWorkerCount(), worker_count);
		}
	}

	return port;
}

bool PhysicalPortManager::DeletePort(std::shared_ptr<PhysicalPort> &port)
{
	if (port == nullptr)
	{
		return false;
	}

	auto lock_guard = std::lock_guard(_port_list_mutex);

	auto key = std::make_pair(port->GetType(), port->GetAddress());
	auto item = _port_list.find(key);

	if (item == _port_list.end())
	{
		OV_ASSERT2(false);
		return false;
	}

	port->DecreaseRefCount();

	if (port->GetRefCount() == 0)
	{
		// last reference
		_port_list.erase(item);
		port->Close();
	}

	return true;
}
