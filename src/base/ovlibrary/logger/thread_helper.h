//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include <pthread.h>

#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace ov::logger
{
	constexpr size_t MAX_THREAD_NAME_LENGTH = 16;

	namespace internal
	{
		class Helper;
	}

	// This class maps spdlog's thread id to pthread id.
	// Usage: When a new thread is created, just declare this variable at the entry of the thread proc.
	// Upon declaration, the constructor adds the mapping, and later the destructor removes it by RAII.
	class ThreadHelper
	{
		friend class Helper;

	public:
		ThreadHelper();
		~ThreadHelper();

		static pthread_t GetPthreadId(size_t thread_id);

	private:
#ifdef DEBUG
		size_t _thread_id;
		pthread_t _pthread_id;
#endif	// DEBUG

		std::shared_ptr<internal::Helper> _helper;
	};
}  // namespace ov::logger
