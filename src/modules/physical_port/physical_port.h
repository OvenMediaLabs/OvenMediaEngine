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

	// Grows `SO_SNDBUF` / `SO_RCVBUF` of every datagram socket of a UDP port to at least the given sizes
	// (bytes, `0` == leave alone). Buffers are never shrunk, so when several binders share one UDP port
	// (e.g. WebRTC provider and publisher ICE candidates on the same port) the largest request wins
	// regardless of which one created the port. No-op for TCP/SRT ports.
	bool GrowDatagramBufferSizes(int send_buffer_size, int recv_buffer_size);

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
							  int send_buffer_size,
							  int recv_buffer_size,
							  const OnSocketCreated on_socket_created);

	// Logs the `SO_SNDBUF` / `SO_RCVBUF` sizes the kernel granted to the datagram sockets and warns
	// when they are capped below what was requested (`net.core.wmem_max` / `net.core.rmem_max`)
	void LogDatagramBufferSizes(int send_buffer_size, int recv_buffer_size) const;

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
	std::vector<std::shared_ptr<ov::DatagramSocket>> _datagram_sockets;

	std::atomic<int> _ref_count{0};

	// Because the life cycle of PhysicalPort is the same as that of the OME now, we do not need to use mutex for _observer_list
	std::vector<PhysicalPortObserver *> _observer_list;
};
