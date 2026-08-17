//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "physical_port.h"

#include <algorithm>

#include "physical_port_private.h"

//
// Format: <Name> + "-" + <Type> + <Port>
//
// Example: APISvr-T1234 ("APISvr", IPv6, TCP, Port 1234)
// Example: APISvr-t1234 ("APISvr", IPv4, TCP, Port 1234)
// Example: APISvr-U1234 ("APISvr", IPv6, UCP, Port 1234)
// Example: APISvr-u1234 ("APISvr", IPv4, UCP, Port 1234)
static ov::String GetSocketPoolName(const ov::SocketType type, const char *name, const ov::SocketAddress &address)
{
	ov::String pool_name(name);
	char family_name;

	pool_name = pool_name.Substring(0, 6);

	switch (type)
	{
		case ov::SocketType::Tcp:
			family_name = 'T';
			break;

		case ov::SocketType::Udp:
			family_name = 'U';
			break;

		case ov::SocketType::Srt:
			family_name = 'S';
			break;

		default:
			family_name = '?';
			break;
	}

	switch (address.GetFamily())
	{
		case ov::SocketFamily::Inet:
			family_name = ::tolower(family_name);
			break;

		case ov::SocketFamily::Inet6:
			family_name = ::toupper(family_name);
			break;

		default:
			family_name = '?';
			break;
	}

	pool_name.AppendFormat("-%c%d", family_name, address.Port());

	return pool_name;
}

PhysicalPort::~PhysicalPort()
{
	OV_ASSERT(_observer_list.empty(), "Observers should be removed before destroying %s physical port", _name.CStr());
}

bool PhysicalPort::Create(const char *name,
						  ov::SocketType type,
						  const ov::SocketAddress &address,
						  int worker_count,
						  bool thread_per_socket,
						  int send_buffer_size,
						  int recv_buffer_size,
						  const OnSocketCreated on_socket_created)
{
	if ((_server_socket != nullptr) || (_datagram_sockets.empty() == false))
	{
		logte("Physical port already created");
		OV_ASSERT2((_server_socket == nullptr) && (_datagram_sockets.empty()));
		return false;
	}

	_name = name;

	logtt("Trying to start physical port [%s] on %s/%s (worker: %d, send_buffer_size: %d, recv_buffer_size: %d)...",
		  name,
		  address.ToString().CStr(), ov::StringFromSocketType(type),
		  worker_count, send_buffer_size, recv_buffer_size);

	bool result = false;

	switch (type)
	{
		case ov::SocketType::Srt:
		case ov::SocketType::Tcp:
			result = CreateServerSocket(name, type, address, worker_count, thread_per_socket, send_buffer_size, recv_buffer_size, on_socket_created);
			break;

		case ov::SocketType::Udp:
			result = CreateDatagramSocket(name, type, address, worker_count, thread_per_socket, on_socket_created);
			break;

		case ov::SocketType::Unknown:
			logte("Not supported socket type: %s", ov::StringFromSocketType(type));
			break;
	}

	return result;
}

bool PhysicalPort::CreateServerSocket(
	const char *name,
	ov::SocketType type,
	const ov::SocketAddress &address,
	int worker_count,
	bool thread_per_socket,
	int send_buffer_size,
	int recv_buffer_size,
	const OnSocketCreated on_socket_created)
{
	_socket_pool = ov::SocketPool::Create(GetSocketPoolName(type, name, address), type, thread_per_socket);

	if (_socket_pool != nullptr)
	{
		if (_socket_pool->Initialize(worker_count))
		{
			auto socket = _socket_pool->AllocSocket<ov::ServerSocket>(address.GetFamily(), _socket_pool);

			if (socket != nullptr)
			{
				if (socket->Prepare(
						address,
						on_socket_created,
						std::bind(&PhysicalPort::OnClientConnectionStateChanged, this,
								  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
						std::bind(&PhysicalPort::OnClientData, this,
								  std::placeholders::_1, std::placeholders::_2),
						send_buffer_size, recv_buffer_size, 4096))
				{
					_type = type;
					_server_socket = socket;
					_address = address;

					return true;
				}

				_socket_pool->ReleaseSocket(socket);
			}

			OV_SAFE_RESET(_socket_pool, nullptr, _socket_pool->Uninitialize(), _socket_pool);
		}
		else
		{
			_socket_pool = nullptr;
		}
	}

	return false;
}

bool PhysicalPort::CreateDatagramSocket(
	const char *name,
	ov::SocketType type,
	const ov::SocketAddress &address,
	int worker_count,
	bool thread_per_socket,
	const OnSocketCreated on_socket_created)
{
	_socket_pool = ov::SocketPool::Create(GetSocketPoolName(type, name, address), type, thread_per_socket);

	if (_socket_pool == nullptr)
	{
		return false;
	}

	// Without `SO_REUSEPORT` only a single datagram socket is created, so the caller
	// (`PhysicalPortManager::CreatePort()`) already clamps `worker_count` to 1 on those platforms.
	if (_socket_pool->Initialize(worker_count) == false)
	{
		_socket_pool = nullptr;
		return false;
	}

	auto datagram_callback = std::bind(
		&PhysicalPort::OnDatagram, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

#ifdef SO_REUSEPORT
	// Set `SO_REUSEPORT` before `Bind()` via the additional-options callback.
	// On failure, returns an error so the loop skips this socket;
	// the caller falls back to a single non-reuseport socket only if every socket fails (`succeeded_count == 0`).
	auto reuseport_callback = [on_socket_created](const std::shared_ptr<ov::Socket> &socket) -> std::shared_ptr<ov::Error> {
		if (socket->SetSockOpt<int>(SO_REUSEPORT, 1) == false)
		{
			logtw("Failed to set SO_REUSEPORT on socket fd %d", socket->GetNativeHandle());
			return ov::Error::CreateError(
				"PhyPort",
				"Failed to set SO_REUSEPORT on socket fd %d", socket->GetNativeHandle());
		}

		return (on_socket_created != nullptr)
				   ? on_socket_created(socket)
				   : nullptr;
	};

	// Create `worker_count` sockets with `SO_REUSEPORT` - the kernel distributes incoming UDP packets across them by 4-tuple hash.
	// `AllocSocket()` internally calls `GetLeastBusyWorker()`, but since we allocate exactly N sockets to N workers it's effectively a 1:1 assignment.
	auto succeeded_count = 0;

	for (auto index = 0; index < worker_count; index++)
	{
		auto socket = _socket_pool->AllocSocket<ov::DatagramSocket>(address.GetFamily());

		if (socket == nullptr)
		{
			logtw("Failed to allocate datagram socket %d/%d for %s",
				  index + 1, worker_count, address.ToString().CStr());

			continue;
		}

		if (socket->Prepare(address, reuseport_callback, datagram_callback) == false)
		{
			logtw("Failed to prepare datagram socket %d/%d for %s",
				  index + 1, worker_count, address.ToString().CStr());

			_socket_pool->ReleaseSocket(socket);
			continue;
		}

		logtd("Created SO_REUSEPORT datagram socket fd=%d, socket %d/%d, address=%s",
			  socket->GetNativeHandle(), index + 1, worker_count,
			  address.ToString().CStr());

		_datagram_sockets.push_back(socket);
		succeeded_count++;
	}

	if (succeeded_count > 0)
	{
		if (succeeded_count < worker_count)
		{
			logtw("Only %d of %d SO_REUSEPORT sockets created for %s",
				  succeeded_count, worker_count, address.ToString().CStr());
		}

		_type	 = type;
		_address = address;
		return true;
	}

	// All `SO_REUSEPORT` sockets failed at runtime - fall back to a single socket without it.
	// The pool intentionally keeps its `worker_count` workers (some now idle): the caller requested that
	// many and `GetWorkerCount()` must stay consistent with the request, so we do not resize the pool on
	// this rare degraded path. The partial-failure case above leaves idle workers for the same reason.
	logtw("SO_REUSEPORT sockets could not be created, falling back to a single datagram socket for %s",
		  address.ToString().CStr());
#else	// SO_REUSEPORT
	// `SO_REUSEPORT` is unavailable at build time - create a single datagram socket directly to avoid redundant work and log spam
	logtw("SO_REUSEPORT is not supported on this platform, creating a single datagram socket for %s",
		  address.ToString().CStr());
#endif	// SO_REUSEPORT

	auto socket = _socket_pool->AllocSocket<ov::DatagramSocket>(address.GetFamily());

	if ((socket != nullptr) && socket->Prepare(address, on_socket_created, datagram_callback))
	{
		logti("Created fallback datagram socket fd=%d, address=%s",
			  socket->GetNativeHandle(), address.ToString().CStr());

		_datagram_sockets.push_back(socket);
		_type	 = type;
		_address = address;
		return true;
	}

	if (socket != nullptr)
	{
		_socket_pool->ReleaseSocket(socket);
	}

	logte("All datagram socket creations failed for %s", address.ToString().CStr());
	OV_SAFE_RESET(_socket_pool, nullptr, _socket_pool->Uninitialize(), _socket_pool);

	return false;
}

void PhysicalPort::OnClientConnectionStateChanged(const std::shared_ptr<ov::ClientSocket> &client, ov::SocketConnectionState state, const std::shared_ptr<ov::Error> &error)
{
	switch (state)
	{
		case ov::SocketConnectionState::Connected: {
			logtt("New client is connected: %s", client->ToString().CStr());

			// Notify observers
			auto func = std::bind(&PhysicalPortObserver::OnConnected, std::placeholders::_1, std::static_pointer_cast<ov::Socket>(client));
			std::for_each(_observer_list.begin(), _observer_list.end(), func);

			break;
		}

		case ov::SocketConnectionState::Disconnect: {
			logtt("Disconnected by server: %s", client->ToString().CStr());

			// Notify observers
			auto func = bind(&PhysicalPortObserver::OnDisconnected, std::placeholders::_1, std::static_pointer_cast<ov::Socket>(client), PhysicalPortDisconnectReason::Disconnect, nullptr);
			std::for_each(_observer_list.begin(), _observer_list.end(), func);

			break;
		}

		case ov::SocketConnectionState::Disconnected: {
			logtt("Client is disconnected: %s", client->ToString().CStr());

			// Notify observers
			auto func = bind(&PhysicalPortObserver::OnDisconnected, std::placeholders::_1, std::static_pointer_cast<ov::Socket>(client), PhysicalPortDisconnectReason::Disconnected, nullptr);
			std::for_each(_observer_list.begin(), _observer_list.end(), func);

			break;
		}

		case ov::SocketConnectionState::Error: {
			logtt("Client is disconnected with error: %s (%s)", client->ToString().CStr(), (error != nullptr) ? error->What() : "N/A");

			// Notify observers
			auto func = bind(&PhysicalPortObserver::OnDisconnected, std::placeholders::_1, std::static_pointer_cast<ov::Socket>(client), PhysicalPortDisconnectReason::Error, error);
			std::for_each(_observer_list.begin(), _observer_list.end(), func);

			break;
		}
	}
}

void PhysicalPort::OnClientData(const std::shared_ptr<ov::ClientSocket> &client, const std::shared_ptr<const ov::Data> &data)
{
	auto sock = client->GetSocket();

	if (sock.IsValid())
	{
		// Notify observers
		auto func = std::bind(
			&PhysicalPortObserver::OnDataReceived,
			std::placeholders::_1,
			std::static_pointer_cast<ov::Socket>(client),
			*(client->GetRemoteAddress().get()),
			std::ref(data));

		std::for_each(_observer_list.begin(), _observer_list.end(), func);
	}
	else
	{
		logtw("Received data %zu bytes from disconnected client", data->GetLength());
	}
}

void PhysicalPort::OnDatagram(const std::shared_ptr<ov::DatagramSocket> &client, const ov::SocketAddressPair &address_pair, const std::shared_ptr<ov::Data> &data)
{
	// Notify observers
	for (auto &observer : _observer_list)
	{
		observer->OnDatagramReceived(client, address_pair, data);
	}
}

bool PhysicalPort::Close()
{
	// `Close()` may be called directly (outside `PhysicalPortManager::DeletePort()`), so make it idempotent.
	if (_socket_pool == nullptr)
	{
		return true;
	}

	if (_server_socket != nullptr)
	{
		_socket_pool->ReleaseSocket(_server_socket);
		_server_socket = nullptr;
	}

	for (auto &datagram_socket : _datagram_sockets)
	{
		_socket_pool->ReleaseSocket(datagram_socket);
	}
	_datagram_sockets.clear();

	_socket_pool->Uninitialize();
	_socket_pool = nullptr;

	_observer_list.clear();

	return true;
}

ov::SocketState PhysicalPort::GetState() const
{
	auto socket = GetSocket();

	if (socket != nullptr)
	{
		return socket->GetState();
	}

	return ov::SocketState::Closed;
}

std::shared_ptr<const ov::Socket> PhysicalPort::GetSocket() const
{
	switch (_type)
	{
		case ov::SocketType::Tcp:
			[[fallthrough]];
		default:
			OV_ASSERT2(_server_socket != nullptr);
			return _server_socket;

		case ov::SocketType::Udp:
			OV_ASSERT2(_datagram_sockets.empty() == false);
			return _datagram_sockets.empty() ? nullptr : _datagram_sockets.front();
	}
}

std::shared_ptr<ov::Socket> PhysicalPort::GetSocket()
{
	switch (_type)
	{
		case ov::SocketType::Tcp:
			[[fallthrough]];
		default:
			return _server_socket;

		case ov::SocketType::Udp:
			return _datagram_sockets.empty() ? nullptr : _datagram_sockets.front();
	}
}

bool PhysicalPort::AddObserver(PhysicalPortObserver *observer)
{
	auto item = std::find(_observer_list.begin(), _observer_list.end(), observer);
	if (item == _observer_list.end())
	{
		_observer_list.push_back(observer);
	}

	return true;
}

bool PhysicalPort::RemoveObserver(PhysicalPortObserver *observer)
{
	auto item = std::find(_observer_list.begin(), _observer_list.end(), observer);
	if (item == _observer_list.end())
	{
		return false;
	}

	_observer_list.erase(item);

	return true;
}

ov::String PhysicalPort::ToString() const
{
	ov::String description;

	description.Format("<PhysicalPort: %p, type: %d, address: %s, ref_count: %d",
					   this, static_cast<int>(_type), _address.ToString().CStr(), static_cast<int>(_ref_count));

	if (_server_socket != nullptr)
	{
		description.AppendFormat(", socket: %s", _server_socket->ToString().CStr());
	}

	if (_datagram_sockets.empty() == false)
	{
		auto count = _datagram_sockets.size();
		description.AppendFormat(", datagram_sockets: %zu [", count);

		for (size_t index = 0; index < count; index++)
		{
			if (index > 0)
			{
				description.Append(", ");
			}

			description.AppendFormat("fd: %d", _datagram_sockets[index]->GetNativeHandle());
		}

		description.Append(']');
	}

	description.Append('>');

	return description;
}
