//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../socket.h"
#include "../socket_datastructure.h"

namespace ov
{
	class SocketPool;

	class SocketPoolWorker : public EnableSharedFromThis<SocketPoolWorker>
	{
	protected:
		friend class SocketPool;
		friend class Socket;

		OV_SOCKET_DECLARE_PRIVATE_TOKEN();

		struct SocketEntry
		{
			// Tells a TCP/UDP registration apart from an earlier one that used the same native handle
			uint32_t generation = 0;
			std::shared_ptr<Socket> socket;
		};

	public:
		// SocketPoolWorker can only be created within SocketPool
		SocketPoolWorker(PrivateToken token, const std::shared_ptr<SocketPool> &pool);
		~SocketPoolWorker() override;

		bool Initialize();
		bool Uninitialize();

		int GetNativeHandle() const;

		template <typename Tsocket = Socket, typename... Targuments>
		std::shared_ptr<Tsocket> AllocSocket(const SocketFamily family, Targuments... args)
		{
			auto socket = std::make_shared<Tsocket>(Socket::PrivateToken{nullptr}, this->GetSharedPtr(), args...);

			if (PrepareSocket(socket, family))
			{
				return socket;
			}

			return nullptr;
		}

		bool ReleaseSocket(const std::shared_ptr<Socket> &socket);

		String ToString() const;

	protected:
		SocketType GetType() const;

		static bool Compare(const std::shared_ptr<SocketPoolWorker> &worker1,
							const std::shared_ptr<SocketPoolWorker> &worker2)
		{
			return worker1->_socket_count < worker2->_socket_count;
		}

		bool PrepareSocket(std::shared_ptr<Socket> socket, const SocketFamily family);

		void IncreaseSocketCount()
		{
			_socket_count++;
		}

		void DecreaseSocketCount()
		{
			_socket_count--;
			OV_ASSERT2(_socket_count >= 0);
		}

		bool PrepareEpoll();

		void GarbageCollection();

		void CallbackTimedOutConnections();

		void ThreadProc();

		bool AddToEpoll(const std::shared_ptr<Socket> &socket);
		// `polled_sockets` holds at least `EpollMaxEvents` entries.
		// `polled_sockets[i]` is set to the socket of the i-th returned event, or `nullptr` if it was already deleted,
		// in which case the caller skips the event (`_epoll_events[i]` is not filled for SRT).
		int EpollWait(std::vector<std::shared_ptr<Socket>> &polled_sockets, int timeout_msec = Infinite);
		bool DeleteFromEpoll(const std::shared_ptr<Socket> &socket);

		// Erases the entry only if it still belongs to `socket`.
		// The caller's `socket` keeps the object alive, so the erase never runs its destructor under the lock.
		void RemoveFromSocketMap(const std::shared_ptr<Socket> &socket) OV_REQUIRES(_socket_map_mutex);

		void ConvertSrtEventToEpollEvent(const SRT_EPOLL_EVENT &srt_event, epoll_event *event);

		void AddToConnectionTimedOutQueue(const std::shared_ptr<Socket> &socket);
		void EnqueueToDispatchLater(const std::shared_ptr<Socket> &socket);
		void EnqueueToCloseCallbackLater(const std::shared_ptr<Socket> &socket, std::shared_ptr<SocketAsyncInterface> callback);
		void EnqueueToCheckConnectionTimeOut(const std::shared_ptr<Socket> &socket, int timeout_msec);

		void DispatchSocketEventsIfNeeded();
		void CallCloseCallbackIfNeeded();

	protected:
		std::shared_ptr<SocketPool> _pool;

		// Sockets allocated from this worker and not yet closed, used to pick the least busy worker.
		// `SocketPool` increments it on allocation and decrements it again when the socket cannot be created.
		// Otherwise the first close of the socket decrements it, or the destructor of a socket that was never closed.
		std::atomic<int> _socket_count{0};

		// Sockets registered to epoll, updated as soon as `AddToEpoll()`/`DeleteFromEpoll()` is called.
		// `EpollWait()` copies the sockets of the polled events from here under the lock,
		// so a socket deleted by another thread stays alive until its events are handled.
		mutable Mutex _socket_map_mutex;
		// key: native handle of SocketWrapper
		std::unordered_map<int, SocketEntry> _socket_map OV_GUARDED_BY(_socket_map_mutex);
		uint32_t _last_generation OV_GUARDED_BY(_socket_map_mutex) = 0;

		// Socket failed to send data for too long must be forced to shut down in the future
		StopWatch _gc_interval;
		// Worker thread only
		std::map<socket_id_t, std::shared_ptr<Socket>> _gc_candidates;

		// A queue for handling errors such as connection timeout in nonblocking mode.
		inline static DelayQueue _connection_callback_queue{"ConnectionCB"};
		inline static std::once_flag _connection_callback_queue_start_flag;
		mutable Mutex _connection_timed_out_queue_mutex;
		std::deque<std::shared_ptr<Socket>> _connection_timed_out_queue OV_GUARDED_BY(_connection_timed_out_queue_mutex);

		// Common variables
		std::thread _epoll_thread;
		std::atomic<bool> _stop_epoll_thread = true;
		std::vector<epoll_event> _epoll_events;
		int _last_epoll_event_count = 0;

		// Occasionally, events on sockets need to be dispatched, even without events from epolls.
		// The queue used in this case
		Mutex _sockets_to_dispatch_mutex;
		std::unordered_map<std::shared_ptr<Socket>, std::shared_ptr<Socket>> _sockets_to_dispatch OV_GUARDED_BY(_sockets_to_dispatch_mutex);

		Mutex _sockets_to_call_close_callback_mutex;
		std::unordered_map<std::shared_ptr<Socket>, std::shared_ptr<SocketAsyncInterface>> _sockets_to_call_close_callback OV_GUARDED_BY(_sockets_to_call_close_callback_mutex);

		// Related to epoll
		socket_t _epoll		 = InvalidSocket;

		// Related to SRT
		SRTSOCKET _srt_epoll = InvalidSocket;
		std::vector<SRT_EPOLL_EVENT> _srt_epoll_events;
	};

}  // namespace ov