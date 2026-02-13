//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "socket_pool.h"

#include "../socket_private.h"

#define OV_LOG_PREFIX_FORMAT "[%p] "
#define OV_LOG_PREFIX_VALUE this

namespace ov
{
	SocketPool::SocketPool(PrivateToken token, const char *name, SocketType type, bool thread_per_socket)
		: _name(name),
		  _type(type),
		  _thread_per_socket(thread_per_socket)
	{
	}

	SocketPool::~SocketPool()
	{
		// Verify that the epoll is closed normally
		std::lock_guard lock_guard(_worker_list_mutex);

#if DEBUG
		for (auto &worker : _worker_list)
		{
			OV_ASSERT(worker->_epoll == InvalidSocket, "Epoll is not uninitialized");
		}
#endif	// DEBUG
	}

	SocketType SocketPool::GetType() const
	{
		return _type;
	}

	bool SocketPool::Initialize(int worker_count)
	{
		if (_initialized)
		{
			logaw("Epoll is already initialized");
			return false;
		}

		if (_thread_per_socket)
		{
			_initialized  = true;
			logai("Socket pool (%s) is initialized with thread per socket mode", ToString().CStr());

			_timer.Push(
				[this](void *parameter) -> ov::DelayQueueAction {
					decltype(_worker_list) release_worker_list;

					{
						std::lock_guard lock_guard(_worker_list_mutex);

						for (auto iterator = _worker_list.begin(); iterator != _worker_list.end();)
						{
							auto worker = *iterator;

							if (worker->_socket_count == 0)
							{
								release_worker_list.push_back(worker);
								iterator = _worker_list.erase(iterator);
							}
							else
							{
								++iterator;
							}
						}
					}

					for (auto worker : release_worker_list)
					{
						logat("Releasing idle worker: %s", worker->ToString().CStr());
						worker->Uninitialize();
					}

					return ov::DelayQueueAction::Repeat;
				},
				1000);
			_timer.Start();

			return true;
		}

		if (worker_count < 0)
		{
			logae("Invalid worker count: %d", worker_count);
			OV_ASSERT2(false);
			return false;
		}

		if (worker_count > 1024)
		{
			logaw("Too many workers: %d, is this intended?", worker_count);
		}

		{
			std::vector<std::shared_ptr<SocketPoolWorker>> worker_list;

			auto pool	   = GetSharedPtr();
			bool succeeded = true;

			logat("Trying to initialize socket pool with %d workers...", worker_count);

			for (int index = 0; index < worker_count; index++)
			{
				auto instance = CreateWorker(pool);
				if (instance == nullptr)
				{
					succeeded = false;
					break;
				}

				worker_list.emplace_back(instance);
			}

			if (succeeded)
			{
				{
					std::lock_guard lock_guard(_worker_list_mutex);
					_worker_list.insert(
						_worker_list.end(),
						std::make_move_iterator(worker_list.begin()),
						std::make_move_iterator(worker_list.end()));
				}

				logat("%d workers were created successfully", worker_count);
				_initialized = true;
			}
			else
			{
				logae("An error occurred while creating workers. Rollbacking...", worker_count);

				// Rollback
				UninitializeWorkers(worker_list);
			}
		}

		return _initialized;
	}

	bool SocketPool::UninitializeWorkers(const std::vector<std::shared_ptr<SocketPoolWorker>> &worker_list)
	{
		for (auto worker : worker_list)
		{
			worker->Uninitialize();
		}

		return true;
	}

	bool SocketPool::Uninitialize()
	{
		logat("Trying to uninitialize socket pool...");

		std::vector<std::shared_ptr<SocketPoolWorker>> worker_list;

		{
			std::lock_guard lock_guard(_worker_list_mutex);
			worker_list = std::move(_worker_list);
		}

		return UninitializeWorkers(worker_list);
	}

	String SocketPool::ToString() const
	{
		String description;

		std::lock_guard lock_guard(_worker_list_mutex);

		description.AppendFormat(
			"<SocketPool: %p, workers: %d",
			this, _worker_list.size());

		for (auto &worker : _worker_list)
		{
			description.AppendFormat("\n    %s", worker->ToString().CStr());
		}

		description.Append((_worker_list.size() > 0) ? "\n>" : ">");

		return description;
	}
}  // namespace ov
