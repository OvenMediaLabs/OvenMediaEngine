//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "socket_count_slot.h"

#include <utility>

#include "socket_pool_worker.h"

namespace ov
{
	SocketCountSlot::SocketCountSlot(SocketPoolWorker *worker)
		: _worker(worker)
	{
		_worker->IncreaseSocketCount();
	}

	SocketCountSlot::SocketCountSlot(SocketCountSlot &&other) noexcept
		: _worker(std::exchange(other._worker, nullptr))
	{
	}

	SocketCountSlot &SocketCountSlot::operator=(SocketCountSlot &&other) noexcept
	{
		if (this != &other)
		{
			Reset();
			_worker = std::exchange(other._worker, nullptr);
		}

		return *this;
	}

	SocketCountSlot::~SocketCountSlot()
	{
		Reset();
	}

	void SocketCountSlot::Reset()
	{
		if (_worker != nullptr)
		{
			std::exchange(_worker, nullptr)->DecreaseSocketCount();
		}
	}
}  // namespace ov
