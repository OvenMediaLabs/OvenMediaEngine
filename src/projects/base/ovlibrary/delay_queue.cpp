//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./delay_queue.h"

#include <unistd.h>

#include <thread>

#include "./log.h"
#include "./logger/thread_helper.h"
#include "./ovlibrary_private.h"

namespace ov
{
	DelayQueue::DelayQueue(const char *queue_name)
		: _queue_name(queue_name),

		  _index(0L),

		  _stop(true)
	{
	}

	DelayQueue::~DelayQueue()
	{
		Stop();
	}

	void DelayQueue::Push(const DelayQueueFunction &func, void *parameter, int after_msec)
	{
		ov::LockGuard lock(_mutex);

		int64_t index = _index;
		_index++;

		logtt("Pushing new item: %p (after %d ms)", parameter, after_msec);

		_queue.emplace(index, func, parameter, after_msec);

		logtt("Notifying...");
		_event.SetEvent();
	}

	void DelayQueue::Push(const DelayQueueFunction &func, int after_msec)
	{
		Push(func, nullptr, after_msec);
	}

	ssize_t DelayQueue::GetCount() const
	{
		ov::LockGuard lock(_mutex);

		return _queue.size();
	}

	void DelayQueue::Clear()
	{
		ov::LockGuard lock(_mutex);

		// empty _queue
		std::priority_queue<DelayQueueItem>().swap(_queue);
	}

	bool DelayQueue::Start()
	{
		if (_stop == false)
		{
			// Already running
			return false;
		}

		_stop = false;
		_thread = std::thread(std::bind(&DelayQueue::DispatchThreadProc, this));

		String name;

		if (_queue_name.IsEmpty())
		{
			name = "DQ";
		}
		else
		{
			name.Format("DQ%s", _queue_name.CStr());
		}

		::pthread_setname_np(_thread.native_handle(), name);

		return true;
	}

	bool DelayQueue::Stop()
	{
		if (_stop)
		{
			// Already stopped
			return false;
		}

		_stop = true;
		_event.SetEvent();

		if (_thread.joinable())
		{
			_thread.join();
		}

		return true;
	}

	void DelayQueue::DispatchThreadProc()
	{
		logger::ThreadHelper thread_helper;

		while (_stop == false)
		{
			ov::ReleasableLockGuard lock(_mutex);
			if (_queue.empty())
			{
				logtt("Queue is empty. Waiting for new item...");
				lock.Release();

				_event.Wait();

				logtt("An item is pushed. Processing...");
			}
			else
			{
				// Handle the first enqueued item
				auto first_item = _queue.top();
				lock.Release();

				if (_event.Wait(first_item.time_point) == false)
				{
					// No other items pushed until waiting for first_item.time_point
					DelayQueueAction action = first_item.function(first_item.parameter);

					ov::LockGuard pop_lock(_mutex);
					// If we enter this step immediately after Clear(), there will be a problem
					if (_queue.empty() == false)
					{
						_queue.pop();

						if (action == DelayQueueAction::Repeat)
						{
							first_item.RecalculateTimePoint();
							_queue.push(first_item);
						}
					}
				}
				else
				{
					// Another item was pushed while waiting for first_item.time_point

					// Newly pushed items may be pointing to a time_point smaller than first_item.time_point,
					// which needs to be recalculated
					logtt("Another item is pushed while waiting the condition");
				}
			}
		}
	}
}  // namespace ov