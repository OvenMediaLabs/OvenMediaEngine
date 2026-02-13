//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovsocket/ovsocket.h>

#include <functional>
#include <memory>
#include <thread>

#include "physical_port_observer.h"

class PhysicalPortManager;

class PhysicalPort : public ov::EnableSharedFromThis<PhysicalPort>
{
protected:
	OV_SOCKET_DECLARE_PRIVATE_TOKEN();

	friend class PhysicalPortManager;

public:
	// Called when a socket is allocated (before the socket is prepared)
	using OnSocketCreated = std::function<std::shared_ptr<ov::Error>(const std::shared_ptr<ov::Socket> &socket)>;

public:
	explicit PhysicalPort(PrivateToken token)
	{
	}

	virtual ~PhysicalPort();

	bool Create(const char *name,
				ov::SocketType type,
				const ov::SocketAddress &address,
				int worker_count,
				bool thread_per_socket,
				int send_buffer_size,
				int recv_buffer_size,
				const OnSocketCreated on_socket_created);

	bool Close();

	void IncreaseRefCount()
	{
		_ref_count++;
	}

	void DecreaseRefCount()
	{
		_ref_count--;
	}

	int GetRefCount() const
	{
		return _ref_count;
	}

	ov::SocketState GetState() const;

	ov::SocketType GetType() const
	{
		return _type;
	}

	const ov::SocketAddress &GetAddress() const
	{
		return _address;
	}

	std::shared_ptr<const ov::Socket> GetSocket() const;
	std::shared_ptr<ov::Socket> GetSocket();

	int GetWorkerCount() const
	{
		return _socket_pool->GetWorkerCount();
	}

	bool AddObserver(PhysicalPortObserver *observer);

	bool RemoveObserver(PhysicalPortObserver *observer);

	ov::String ToString() const;

protected:
	bool CreateServerSocket(const char *name,
							ov::SocketType type,
							const ov::SocketAddress &address,
							int worker_count,
							bool thread_per_socket,
							int send_buffer_size,
							int recv_buffer_size,
							const OnSocketCreated on_socket_created);

	bool CreateDatagramSocket(const char *name,
							  ov::SocketType type,
							  const ov::SocketAddress &address,
							  int worker_count,
							  bool thread_per_socket,
							  const OnSocketCreated on_socket_created);

	// For TCP physical port
	void OnClientConnectionStateChanged(const std::shared_ptr<ov::ClientSocket> &client, ov::SocketConnectionState state, const std::shared_ptr<ov::Error> &error);
	void OnClientData(const std::shared_ptr<ov::ClientSocket> &client, const std::shared_ptr<const ov::Data> &data);

	// For UDP physical port
	void OnDatagram(const std::shared_ptr<ov::DatagramSocket> &client, const ov::SocketAddressPair &address_pair, const std::shared_ptr<ov::Data> &data);

	ov::String _name;
	std::shared_ptr<ov::SocketPool> _socket_pool;

	ov::SocketType _type = ov::SocketType::Unknown;
	ov::SocketAddress _address;

	std::shared_ptr<ov::ServerSocket> _server_socket;
	std::shared_ptr<ov::DatagramSocket> _datagram_socket;

	std::atomic<int> _ref_count{0};

	// Because the life cycle of PhysicalPort is the same as that of the OME now, we do not need to use mutex for _observer_list
	std::vector<PhysicalPortObserver *> _observer_list;
};
