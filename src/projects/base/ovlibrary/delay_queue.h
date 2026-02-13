//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <algorithm>
#include <cstdio>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <atomic>

#include "./event.h"
#include "./string.h"

namespace ov
{
	enum class DelayQueueAction : char
	{
		Stop,
		Repeat
	};

	// Return false if need to stop when using repeat mode
	typedef std::function<DelayQueueAction(void *parameter)> DelayQueueFunction;

	class DelayQueue
	{
	public:
		DelayQueue(const char *queue_name);
		virtual ~DelayQueue();

		void Push(const DelayQueueFunction &func, void *parameter, int after_msec);
		void Push(const DelayQueueFunction &func, int after);
		ssize_t GetCount() const;

		void Clear();

		bool Start();
		bool Stop();

	protected:
		struct DelayQueueItem
		{
		public:
			int64_t index;

			DelayQueueFunction function;
			void *parameter;

			int after_msec;

			std::chrono::time_point<std::chrono::system_clock> time_point;

			DelayQueueItem(int64_t index, DelayQueueFunction function, void *parameter, int after_msec)
				: index(index),

				  function(std::move(function)),
				  parameter(parameter),

				  after_msec(after_msec)
			{
				RecalculateTimePoint();
			}

			void RecalculateTimePoint()
			{
				time_point = std::chrono::system_clock::now() + std::chrono::milliseconds(after_msec);
			}

			bool operator<(const DelayQueueItem &item) const
			{
				// item.interval has a high priority, followed by item.index
				return (time_point == item.time_point) ? (index > item.index) : (time_point > item.time_point);
			}
		};

	protected:
		void DispatchThreadProc();

		ov::String _queue_name;

		int64_t _index;

		std::thread _thread;
		std::atomic<bool> _stop;

		// A queue where the items to be executed are stored
		mutable std::mutex _mutex;
		std::priority_queue<DelayQueueItem> _queue;

		// If Push() is called while DelayQueue is waiting, the priority must be recalculated.
		// _event is used to notice this.
		Event _event;
	};
}  // namespace ov
