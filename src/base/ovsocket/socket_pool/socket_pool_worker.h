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

		void MergeSocketList();

		void GarbageCollection();

		void CallbackTimedOutConnections();

		void ThreadProc();

		bool AddToEpoll(const std::shared_ptr<Socket> &socket);
		int EpollWait(int timeout_msec = Infinite);
		bool DeleteFromEpoll(const std::shared_ptr<Socket> &socket);

		bool ConvertSrtEventToEpollEvent(const SRT_EPOLL_EVENT &srt_event, epoll_event *event);

		void AddToConnectionTimedOutQueue(const std::shared_ptr<Socket> &socket);
		void EnqueueToDispatchLater(const std::shared_ptr<Socket> &socket);
		void EnqueueToCloseCallbackLater(const std::shared_ptr<Socket> &socket, std::shared_ptr<SocketAsyncInterface> callback);
		void EnqueueToCheckConnectionTimeOut(const std::shared_ptr<Socket> &socket, int timeout_msec);

		void DispatchSocketEventsIfNeeded();
		void CallCloseCallbackIfNeeded();

	protected:
		std::shared_ptr<SocketPool> _pool;

		// The number of sockets is determined in advance and managed separately for processing
		// Because excessive concentration may not be properly distributed to the worker,
		// the number of sockets can be specified in advance so that they can be distributed properly.
		std::atomic<int> _socket_count{0};

		// A list of sockets created/deleted from AddToWorker()/DeleteFromEpoll()
		//
		// If DeleteFromEpoll() is called in thread #1 immediately after EpollWait() is called in thread #2,
		// Since there is no longer a reference to std::shared_ptr<Socket>, the socket will be released,
		// and after EpollWait() code will refer to the released socket.
		// To avoid this issue, _socket_map must ensure that it is modified only in the last part of ThreadProc().
		mutable std::mutex _socket_map_mutex;
		// key: native handle of SocketWrapper
		std::map<int, std::shared_ptr<Socket>> _socket_map;
		std::queue<std::shared_ptr<Socket>> _sockets_to_insert;

		// Socket failed to send data for too long must be forced to shut down in the future
		StopWatch _gc_interval;
		std::map<int, std::shared_ptr<Socket>> _gc_candidates;

		// A queue for handling errors such as connection timeout in nonblocking mode.
		inline static std::mutex _connection_callback_queue_mutex;
		inline static DelayQueue _connection_callback_queue{"ConnectionCB"};
		inline static bool _is_first_connection_callback_queue_start = true;
		std::mutex _connection_timed_out_queue_mutex;
		std::deque<std::shared_ptr<Socket>> _connection_timed_out_queue;

		// Common variables
		std::thread _epoll_thread;
		std::atomic<bool> _stop_epoll_thread = true;
		std::vector<epoll_event> _epoll_events;
		int _last_epoll_event_count = 0;

		// Occasionally, events on sockets need to be dispatched, even without events from epolls.
		// The queue used in this case
		std::mutex _sockets_to_dispatch_mutex;
		std::unordered_map<std::shared_ptr<Socket>, std::shared_ptr<Socket>> _sockets_to_dispatch;

		std::mutex _sockets_to_call_close_callback_mutex;
		std::unordered_map<std::shared_ptr<Socket>, std::shared_ptr<SocketAsyncInterface>> _sockets_to_call_close_callback;

		// Related to epoll
		socket_t _epoll = InvalidSocket;

		// Related to SRT
		SRTSOCKET _srt_epoll = InvalidSocket;
		std::vector<SRT_EPOLL_EVENT> _srt_epoll_events;
	};

}  // namespace ov