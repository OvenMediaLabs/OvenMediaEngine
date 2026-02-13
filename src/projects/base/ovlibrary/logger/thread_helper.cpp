//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./thread_helper.h"

#include <spdlog/details/os.h>

#include "../assert.h"

namespace ov::logger
{
	namespace internal
	{
		class Helper
		{
		public:
			void AppendThreadInfo(size_t thread_id, pthread_t pthread_id)
			{
				std::lock_guard lock(_thread_id_map_mutex);
				(*_thread_id_map)[thread_id] = pthread_id;
			}

			void RemoveThreadInfo(size_t thread_id)
			{
				std::lock_guard lock(_thread_id_map_mutex);

#if DEBUG
				if (_thread_id_map->find(thread_id) == _thread_id_map->end())
				{
					OV_ASSERT(false, "Thread id not found in the map.");
				}
#endif	// DEBUG

				_thread_id_map->erase(thread_id);
			}

			pthread_t GetPthreadId(size_t thread_id)
			{
				std::shared_lock lock(_thread_id_map_mutex);

				auto it = _thread_id_map->find(thread_id);

				if (it != _thread_id_map->end())
				{
					return it->second;
				}

#if DEBUG
				OV_ASSERT(false, "Thread id not found in the map.");
#endif	// DEBUG

				return 0;
			}

		private:
			std::shared_mutex _thread_id_map_mutex;
			// key: `thread_id` - This is obtained by calling `thread_id()` of `spdlog`.
			// value: `pthread_id`
			std::shared_ptr<std::unordered_map<size_t, pthread_t>> _thread_id_map = std::make_shared<std::unordered_map<size_t, pthread_t>>();
		};

		static std::shared_ptr<Helper> &GetHelper()
		{
			static auto instance = std::make_shared<Helper>();
			return instance;
		}
	}  // namespace internal

	ThreadHelper::ThreadHelper()
	{
		_helper = internal::GetHelper();
		_helper->AppendThreadInfo(spdlog::details::os::thread_id(), ::pthread_self());

#if DEBUG
		_thread_id	= spdlog::details::os::thread_id();
		_pthread_id = ::pthread_self();
#endif	// DEBUG
	}

	ThreadHelper::~ThreadHelper()
	{
#if DEBUG
		auto thread_id	= spdlog::details::os::thread_id();
		auto pthread_id = ::pthread_self();

		if (thread_id != _thread_id)
		{
			OV_ASSERT(false, "thread_id mismatch in destructor. Expected: %zu, Actual: %zu", _thread_id, thread_id);
		}

		if (::pthread_equal(pthread_id, _pthread_id) == 0)
		{
			OV_ASSERT(false, "pthread_id mismatch in destructor. Expected: %lu, Actual: %lu", static_cast<unsigned long>(_pthread_id), static_cast<unsigned long>(pthread_id));
		}
#endif	// DEBUG

		_helper->RemoveThreadInfo(spdlog::details::os::thread_id());
	}

	pthread_t ThreadHelper::GetPthreadId(size_t thread_id)
	{
		auto helper = internal::GetHelper();

		if (helper != nullptr)
		{
			return helper->GetPthreadId(thread_id);
		}

		return 0;
	}
}  // namespace ov::logger
