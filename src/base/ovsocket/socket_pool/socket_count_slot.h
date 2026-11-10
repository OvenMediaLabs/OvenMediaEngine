//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

namespace ov
{
	class SocketPoolWorker;

	// Counts one socket on a worker until it is reset or destroyed, so the count drops exactly once
	class SocketCountSlot
	{
	public:
		SocketCountSlot() = default;
		explicit SocketCountSlot(SocketPoolWorker *worker);
		SocketCountSlot(SocketCountSlot &&other) noexcept;
		SocketCountSlot &operator=(SocketCountSlot &&other) noexcept;
		SocketCountSlot(const SocketCountSlot &)			= delete;
		SocketCountSlot &operator=(const SocketCountSlot &) = delete;
		~SocketCountSlot();

		void Reset();

	private:
		// The holder keeps the worker alive: a socket through its `_worker`, a queue entry by living in the worker
		SocketPoolWorker *_worker = nullptr;
	};
}  // namespace ov
