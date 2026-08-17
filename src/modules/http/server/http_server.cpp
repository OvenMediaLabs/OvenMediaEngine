//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_server.h"

#include <modules/physical_port/physical_port_manager.h>

#include "./http_server_private.h"
#include "http_server_manager.h"

namespace http
{
	namespace svr
	{
		ov::DelayQueue HttpServer::_repeater{"HTTPTimer"};

		HttpServer::HttpServer(const char *server_name, const char *server_short_name)
			: _server_name(server_name),
			  _server_short_name(server_short_name)
		{
		}

		HttpServer::~HttpServer()
		{
			// PhysicalPort should be stopped before release Server
			OV_ASSERT(_physical_port == nullptr, "%s: Physical port: %s", _server_name.CStr(), _physical_port->ToString().CStr());
		}

		bool HttpServer::Start(const ov::SocketAddress &address, int worker_count, bool enable_http2)
		{
			auto lock_guard = std::lock_guard(_physical_port_mutex);

			if (_physical_port != nullptr)
			{
				logtw("Server is running");
				return false;
			}

			_http2_enabled = enable_http2;

			auto manager = PhysicalPortManager::GetInstance();

			auto physical_port = manager->CreatePort(_server_short_name, ov::SocketType::Tcp, address, worker_count);

			if (physical_port != nullptr)
			{
				if (physical_port->AddObserver(this))
				{
					_physical_port = physical_port;

					_repeater.Push(std::bind(&HttpServer::Repeater, this, std::placeholders::_1), 5 * 1000);
					_repeater.Start();

					return true;
				}
			}

			manager->DeletePort(physical_port);

			return false;
		}

		bool HttpServer::Stop()
		{
			std::shared_ptr<PhysicalPort> physical_port;

			{
				auto lock_guard = std::lock_guard(_physical_port_mutex);
				physical_port = std::move(_physical_port);
			}

			if (physical_port == nullptr)
			{
				// Server is not running
				return false;
			}

			physical_port->RemoveObserver(this);
			PhysicalPortManager::GetInstance()->DeletePort(physical_port);
			physical_port = nullptr;

			// Clean up the client list
			ClientList client_list;

			{
				auto lock_guard = std::lock_guard(_client_list_mutex);
				client_list = std::move(_connection_list);
			}

			for (auto &client : client_list)
			{
				client.second->Close(PhysicalPortDisconnectReason::Disconnect);
			}

			_interceptor_list.clear();

			_repeater.Stop();

			return true;
		}

		ov::DelayQueueAction HttpServer::Repeater(void *parameter)
		{
			std::shared_lock<std::shared_mutex> guard(_client_list_mutex);
			auto client_list = _connection_list;
			guard.unlock();

			for (const auto &item : client_list)
			{
				item.second->OnRepeatTask();
			}

			return ov::DelayQueueAction::Repeat;
		}

		bool HttpServer::IsRunning() const
		{
			auto lock_guard = std::lock_guard(_physical_port_mutex);

			return (_physical_port != nullptr);
		}

		bool HttpServer::IsHttp2Enabled() const
		{
			return _http2_enabled;
		}

		std::shared_ptr<HttpConnection> HttpServer::FindClient(const std::shared_ptr<ov::Socket> &remote)
		{
			std::shared_lock<std::shared_mutex> guard(_client_list_mutex);

			auto item = _connection_list.find(remote.get());

			if (item != _connection_list.end())
			{
				return item->second;
			}

			return nullptr;
		}

		std::shared_ptr<HttpConnection> HttpServer::ProcessConnect(const std::shared_ptr<ov::Socket> &remote)
		{
			logti("Client(%s) is connected on %s", remote->ToString().CStr(), _physical_port->GetAddress().ToString().CStr());

			auto client_socket = std::dynamic_pointer_cast<ov::ClientSocket>(remote);
			if (client_socket == nullptr)
			{
				OV_ASSERT2(false);
				return nullptr;
			}

			std::lock_guard<std::shared_mutex> guard(_client_list_mutex);

			auto http_connection = std::make_shared<HttpConnection>(GetSharedPtr(), client_socket);
			_connection_list[remote.get()] = http_connection;

			return http_connection;
		}

		void HttpServer::OnConnected(const std::shared_ptr<ov::Socket> &remote)
		{
			ProcessConnect(remote);
		}

		void HttpServer::OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data)
		{
			auto client = FindClient(remote);

			if (client == nullptr)
			{
				// This can be called in situations where the client closes the connection from the server at the same time as the data is sent
				return;
			}

			if (remote->IsClosing() == false)
			{
				client->OnDataReceived(data);
			}
			else
			{
				// Avoid processing data received from sockets that attempted to close when keep alive request
			}
		}

		void HttpServer::OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error)
		{
			logtt("HttpServer::OnDisconnected : %d", remote->GetNativeHandle());

			std::shared_ptr<HttpConnection> connection;

			{
				std::lock_guard<std::shared_mutex> guard(_client_list_mutex);

				auto client_iterator = _connection_list.find(remote.get());
				if (client_iterator == _connection_list.end())
				{
					// If an error occurs during TCP or HTTP connection processing, it may not exist in _connection_list.
					logtc("Could not find HTTP connection : %d / size(%zu)", remote->GetNativeHandle(), _connection_list.size());
					return;
				}

				connection = client_iterator->second;
				_connection_list.erase(client_iterator);
			}

			// It comes from HttpConnection::Close()
			if (reason == PhysicalPortDisconnectReason::Disconnect)
			{
				connection->Close(reason);
				logti("Client(%s) has been disconnected by %s",
					  remote->ToString().CStr(), _physical_port->GetAddress().ToString().CStr());
			}
			// It comes from PhysicalPort::Close()
			else
			{
				connection->Close(reason);
				logti("Client(%s) has disconnected from %s",
					  remote->ToString().CStr(), _physical_port->GetAddress().ToString().CStr());
			}
		}

		bool HttpServer::AddInterceptor(const std::shared_ptr<RequestInterceptor> &interceptor)
		{
			std::lock_guard<std::shared_mutex> guard(_interceptor_list_mutex);

			// Find interceptor in the list
			auto item = std::find_if(_interceptor_list.begin(), _interceptor_list.end(), [&](std::shared_ptr<RequestInterceptor> const &value) -> bool {
				return value == interceptor;
			});

			if (item != _interceptor_list.end())
			{
				// interceptor exists in the list
				logtw("%p is already registered", interceptor.get());
				return false;
			}

			_interceptor_list.push_back(interceptor);
			return true;
		}

		std::shared_ptr<RequestInterceptor> HttpServer::FindInterceptor(const std::shared_ptr<HttpExchange> &exchange)
		{
			// Find interceptor for the request
			std::shared_lock<std::shared_mutex> guard(_interceptor_list_mutex);

			for (auto &interceptor : _interceptor_list)
			{
				if (interceptor->IsInterceptorForRequest(exchange))
				{
					return interceptor;
				}
			}

			// TODO(h2) : Check if default interceptor should be used

			return nullptr;
		}

		bool HttpServer::RemoveInterceptor(const std::shared_ptr<RequestInterceptor> &interceptor)
		{
			std::lock_guard<std::shared_mutex> guard(_interceptor_list_mutex);

			// Find interceptor in the list
			auto item = std::find_if(_interceptor_list.begin(), _interceptor_list.end(), [&](std::shared_ptr<RequestInterceptor> const &value) -> bool {
				return value == interceptor;
			});

			if (item == _interceptor_list.end())
			{
				// interceptor does not exists in the list
				logtw("%p is not found.", interceptor.get());
				return false;
			}

			_interceptor_list.erase(item);
			return true;
		}

		ov::Socket *HttpServer::FindClient(ClientIterator iterator)
		{
			std::shared_lock<std::shared_mutex> guard(_client_list_mutex);

			for (auto &client : _connection_list)
			{
				if (iterator(client.second))
				{
					return client.first;
				}
			}

			return nullptr;
		}

		size_t HttpServer::DisconnectIf(ClientIterator iterator)
		{
			std::vector<std::shared_ptr<HttpConnection>> temp_list;

			{
				std::shared_lock<std::shared_mutex> guard(_client_list_mutex);

				for (auto client_iterator : _connection_list)
				{
					auto &client = client_iterator.second;

					if (iterator(client))
					{
						temp_list.push_back(client);
					}
				};
			}

			for (auto client_iterator : temp_list)
			{
				client_iterator->Close(PhysicalPortDisconnectReason::Disconnect);
			}

			return temp_list.size();
		}
	}  // namespace svr
}  // namespace http
