//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/tsa/mutex.h>
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
			LockGuard lock_guard(_worker_list_mutex);
			return static_cast<int>(_worker_list.size());
		}

		template <typename Tsocket = ov::Socket, typename... Targuments>
		std::shared_ptr<Tsocket> AllocSocket(const SocketFamily family, Targuments... args)
		{
			auto [worker, count_slot] = GetLeastBusyWorker();

			return (worker != nullptr) ? worker->AllocSocket<Tsocket>(std::move(count_slot), family, args...) : nullptr;
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
		// The slot counts the new socket on the worker under the list lock,
		// so the next pick sees it and an idle thread-per-socket worker is not released before the socket takes it
		std::pair<std::shared_ptr<SocketPoolWorker>, SocketCountSlot> GetLeastBusyWorker()
		{
			LockGuard lock_guard(_worker_list_mutex);

			if (_uninitialized)
			{
				return {};
			}

			if (_thread_per_socket)
			{
				auto instance = CreateWorker(GetSharedPtr());
				if (instance == nullptr)
				{
					return {};
				}

				_worker_list.push_back(instance);

				return {instance, SocketCountSlot(instance.get())};
			}

			if (_worker_list.size() == 0)
			{
				OV_ASSERT(_initialized, "Worker list is not initialized");
				return {};
			}

			// Use the worker with the smallest number of sockets currently being processed
			auto worker = *std::min_element(_worker_list.begin(), _worker_list.end(), SocketPoolWorker::Compare);

			return {worker, SocketCountSlot(worker.get())};
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

		mutable Mutex _worker_list_mutex;
		std::vector<std::shared_ptr<SocketPoolWorker>> _worker_list OV_GUARDED_BY(_worker_list_mutex);
		// Set when `Uninitialize()` takes the workers, so no worker is created or handed out after that
		bool _uninitialized OV_GUARDED_BY(_worker_list_mutex) = false;

		// Worker releaser
		ov::DelayQueue _timer{"SocketPoolWorkerReleaser"};
	};
}  // namespace ov
