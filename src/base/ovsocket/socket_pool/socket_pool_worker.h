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
		friend class SocketCountSlot;

		OV_SOCKET_DECLARE_PRIVATE_TOKEN();

		// A close callback for the worker thread to run, with the slot that keeps the socket counted until then
		struct CloseCallbackEntry
		{
			std::shared_ptr<SocketAsyncInterface> callback;
			SocketCountSlot count_slot;
		};

		// One event `EpollWait()` returned, with the socket it belongs to
		struct PolledEvent
		{
			std::shared_ptr<Socket> socket;
			epoll_event event;
		};

	public:
		// SocketPoolWorker can only be created within SocketPool
		SocketPoolWorker(PrivateToken token, const std::shared_ptr<SocketPool> &pool);
		~SocketPoolWorker() override;

		bool Initialize();
		// Asks the worker thread to exit, without joining it.
		// The thread sees the request only after its current epoll wait, so stopping many workers requests all first,
		// and `Uninitialize()` then joins them with their waits overlapping instead of adding up.
		void RequestStop();
		bool Uninitialize();

		int GetNativeHandle() const;

		template <typename Tsocket = Socket, typename... Targuments>
		std::shared_ptr<Tsocket> AllocSocket(SocketCountSlot count_slot, const SocketFamily family, Targuments... args)
		{
			auto socket = MakeSocket<Tsocket>(std::move(count_slot), args...);

			return PrepareSocket(socket, family) ? socket : nullptr;
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

		// Builds a socket without creating a handle, so a socket around an open handle takes it here
		template <typename Tsocket, typename... Targuments>
		std::shared_ptr<Tsocket> MakeSocket(SocketCountSlot count_slot, Targuments... args)
		{
			auto socket			= std::make_shared<Tsocket>(Socket::PrivateToken{nullptr}, this->GetSharedPtr(), args...);
			socket->_count_slot = std::move(count_slot);

			return socket;
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
		// Fills `_polled_events` with the events of the sockets still registered, and returns their count
		int EpollWait(int timeout_msec = Infinite);
		bool DeleteFromEpoll(const std::shared_ptr<Socket> &socket);

		void ConvertSrtEventToEpollEvent(const SRT_EPOLL_EVENT &srt_event, epoll_event *event);

		void AddToConnectionTimedOutQueue(const std::shared_ptr<Socket> &socket);
		void EnqueueToDispatchLater(const std::shared_ptr<Socket> &socket);
		void EnqueueToCloseCallbackLater(const std::shared_ptr<Socket> &socket, std::shared_ptr<SocketAsyncInterface> callback, SocketCountSlot count_slot);
		void EnqueueToCheckConnectionTimeOut(const std::shared_ptr<Socket> &socket, int timeout_msec);
		// The process-wide queue that reports a non-blocking connect timeout to its worker
		static DelayQueue &GetConnectionCallbackQueue();

		void DispatchSocketEventsIfNeeded();
		void CallCloseCallbackIfNeeded();

	protected:
		std::shared_ptr<SocketPool> _pool;

		// The number of live `SocketCountSlot`s of this worker, used to pick the least busy worker
		// and to release an idle thread-per-socket worker
		std::atomic<int> _socket_count{0};

		// Sockets registered to epoll, updated as soon as `AddToEpoll()`/`DeleteFromEpoll()` is called.
		// `EpollWait()` copies the sockets of the polled events from here under the lock,
		// so a socket deleted by another thread stays alive until its events are handled.
		mutable Mutex _socket_map_mutex;
		// key: native handle of SocketWrapper
		std::unordered_map<int, std::shared_ptr<Socket>> _socket_map OV_GUARDED_BY(_socket_map_mutex);

		// Socket failed to send data for too long must be forced to shut down in the future
		StopWatch _gc_interval;
		// Worker thread only
		std::map<socket_id_t, std::shared_ptr<Socket>> _gc_candidates;

		// Sockets whose non-blocking connect timed out, for the worker thread to report
		mutable Mutex _connection_timed_out_queue_mutex;
		std::deque<std::shared_ptr<Socket>> _connection_timed_out_queue OV_GUARDED_BY(_connection_timed_out_queue_mutex);

		// Common variables
		std::thread _epoll_thread;
		// Asks the thread to leave its loop, after which it closes the sockets in its map.
		// `AddToEpoll()` refuses a socket once it is set, so no socket joins the map after that.
		std::atomic<bool> _stop_epoll_thread = true;
		// The kernel buffer of a TCP/UDP worker
		std::vector<epoll_event> _epoll_events;
		// Keeps the socket of each polled event alive until the event is handled
		std::vector<PolledEvent> _polled_events;
		int _last_epoll_event_count = 0;

		// Occasionally, events on sockets need to be dispatched, even without events from epolls.
		// The queue used in this case
		Mutex _sockets_to_dispatch_mutex;
		std::unordered_map<std::shared_ptr<Socket>, std::shared_ptr<Socket>> _sockets_to_dispatch OV_GUARDED_BY(_sockets_to_dispatch_mutex);

		// Close callbacks for the worker thread to run
		Mutex _sockets_to_call_close_callback_mutex;
		std::unordered_map<std::shared_ptr<Socket>, CloseCallbackEntry> _sockets_to_call_close_callback OV_GUARDED_BY(_sockets_to_call_close_callback_mutex);

		// Related to epoll.
		// `_epoll` and `_srt_epoll` are written only by `PrepareEpoll()`, before the worker is shared, and never reset,
		// since other threads read them through `GetNativeHandle()`.
		// `Uninitialize()` closes them under `_epoll_mutex`, and `AddToEpoll()`/`DeleteFromEpoll()` call into them
		// under the same lock only while `_stopped` is false, so no call reaches a released number.
		socket_t _epoll		 = InvalidSocket;

		// Related to SRT
		SRTSOCKET _srt_epoll = InvalidSocket;

		// No socket or worker lock is taken while it is held.
		// Logging still takes the logger locks, including `SrtLogHandler()` called by libsrt inside an SRT epoll call.
		Mutex _epoll_mutex;
		// Set by `Uninitialize()` after the join, when it closes the epoll, and before it clears the queues.
		// Every queue push checks it under the queue's lock and drops its input, so a stopped worker holds no socket.
		std::atomic<bool> _stopped = false;
		// The libsrt buffer of an SRT worker
		std::vector<SRT_EPOLL_EVENT> _srt_epoll_events;
	};

}  // namespace ov