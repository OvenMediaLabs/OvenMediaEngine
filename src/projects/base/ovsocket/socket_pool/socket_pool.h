//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdexcept>

#include "socket_pool_worker.h"

namespace ov
{
	class Socket;

	class SocketPool : public EnableSharedFromThis<SocketPool>
	{
	protected:
		OV_SOCKET_DECLARE_PRIVATE_TOKEN();

	public:
		// SocketPool can only be created using SocketPool::Create()
		SocketPool(PrivateToken token, const char *name, SocketType type, bool thread_per_socket);
		~SocketPool() override;

		static std::shared_ptr<SocketPool> Create(const char *name, SocketType type, bool thread_per_socket)
		{
			return std::make_shared<SocketPool>(PrivateToken{nullptr}, name, type, thread_per_socket);
		}

		static std::shared_ptr<SocketPool> GetPool(std::shared_ptr<SocketPool> &pool, std::once_flag &once_flag, const char *name, SocketType type)
		{
			try
			{
				std::call_once(
					once_flag,
					[name, type, &pool]() {
						auto new_pool = Create(name, type, false);

						if ((new_pool != nullptr) && new_pool->Initialize(1))
						{
							pool = std::move(new_pool);
							return;
						}

						throw std::runtime_error("Could not initialize socket pool");
					});
			}
			catch (const std::runtime_error &)
			{
				return nullptr;
			}

			return pool;
		}

		static std::shared_ptr<SocketPool> GetTcpPool()
		{
			static std::shared_ptr<SocketPool> pool;
			static std::once_flag once_flag;

			return GetPool(pool, once_flag, "DefTcp", SocketType::Tcp);
		}

		static std::shared_ptr<SocketPool> GetUdpPool()
		{
			static std::shared_ptr<SocketPool> pool;
			static std::once_flag once_flag;

			return GetPool(pool, once_flag, "DefUdp", SocketType::Udp);
		}

		ov::String GetName() const
		{
			return _name;
		}

		SocketType GetType() const;

		bool Initialize(int worker_count);

		int GetWorkerCount() const
		{
			std::lock_guard lock_guard(_worker_list_mutex);
			return static_cast<int>(_worker_list.size());
		}

		template <typename Tsocket = ov::Socket, typename... Targuments>
		std::shared_ptr<Tsocket> AllocSocket(const SocketFamily family, Targuments... args)
		{
			std::shared_ptr<SocketPoolWorker> worker = GetLeastBusyWorker();
			if (worker != nullptr)
			{
				auto socket = worker->AllocSocket<Tsocket>(family, args...);

				if (socket == nullptr)
				{
					// Rollback
					worker->DecreaseSocketCount();
				}

				return socket;
			}

			return nullptr;
		}

		bool ReleaseSocket(const std::shared_ptr<Socket> &socket)
		{
			return socket->GetSocketPoolWorker()->ReleaseSocket(socket);
		}

		bool Uninitialize();

		String ToString() const;

		bool IsThreadPerSocket() const
		{
			return _thread_per_socket;
		}

	protected:
		// This method will increase the number of sockets for that worker by 1
		std::shared_ptr<SocketPoolWorker> GetLeastBusyWorker()
		{
			std::lock_guard lock_guard(_worker_list_mutex);

			if (_thread_per_socket)
			{
				auto instance = CreateWorker(GetSharedPtr());
				if (instance != nullptr)
				{
					instance->IncreaseSocketCount();
					_worker_list.push_back(instance);
				}

				return instance;
			}

			if (_worker_list.size() == 0)
			{
				OV_ASSERT(_initialized, "Worker list is not initialized");
				return nullptr;
			}

			// Use the worker with the smallest number of sockets currently being processed
			auto worker = *std::min_element(_worker_list.begin(), _worker_list.end(), SocketPoolWorker::Compare);

			worker->IncreaseSocketCount();

			return worker;
		}

		std::shared_ptr<SocketPoolWorker> CreateWorker(const std::shared_ptr<ov::SocketPool> &pool)
		{
			OV_ASSERT2(pool != nullptr);

			auto instance = std::make_shared<SocketPoolWorker>(SocketPoolWorker::PrivateToken{nullptr}, pool);

			if (instance->Initialize())
			{
				return instance;
			}

			return nullptr;
		}

		bool UninitializeWorkers(const std::vector<std::shared_ptr<SocketPoolWorker>> &worker_list);

		ov::String _name;

		SocketType _type		= SocketType::Unknown;

		bool _thread_per_socket = false;

		bool _initialized		= false;

		mutable std::mutex _worker_list_mutex;
		std::vector<std::shared_ptr<SocketPoolWorker>> _worker_list;

		// Worker releaser
		ov::DelayQueue _timer{"SocketPoolWorkerReleaser"};
	};
}  // namespace ov
