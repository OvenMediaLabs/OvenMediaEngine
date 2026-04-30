//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

// This code was written by Rudolfs Bundulis. (Thank you!)
#if IS_MACOS
// OV_LOG_TAG is required by the logte/logw/logi macros and must be defined
// at file scope before any logging call in a header.
#ifndef OV_LOG_TAG
#define OV_LOG_TAG "EpollWrapper"
#endif
#	include <mutex>
#	include <unordered_map>
#	include <sys/event.h>

typedef union epoll_data
{
	void *ptr;
	int fd;
	uint32_t u32;
	uint64_t u64;
} epoll_data_t;

struct epoll_event
{
	uint32_t events;
	epoll_data_t data;
};

static inline int epoll_create1(int)
{
	return kqueue();
}

// epoll_ctl flags
constexpr int EPOLL_CTL_ADD = 1;
constexpr int EPOLL_CTL_DEL = 2;

// epoll_event event values
constexpr int EPOLLIN = 0x0001;
constexpr int EPOLLOUT = 0x0002;
constexpr int EPOLLHUP = 0x0004;
constexpr int EPOLLERR = 0x0008;
constexpr int EPOLLRDHUP = 0x0010;
constexpr int EPOLLPRI = 0x0020;
constexpr int EPOLLRDNORM = 0x0040;
constexpr int EPOLLRDBAND = 0x0080;
constexpr int EPOLLWRNORM = 0x0100;
constexpr int EPOLLWRBAND = 0x0200;
constexpr int EPOLLMSG = 0x0400;
constexpr int EPOLLWAKEUP = 0x0800;
constexpr int EPOLLONESHOT = 0x1000;
constexpr int EPOLLET = 0x2000;

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

// macOS does not have a MSG_NOSIGNAL, has a SO_NOSIGPIPE, need to test to understand equality
#	define MSG_NOSIGNAL 0x2000

/*
	kqueue-based epoll emulation for macOS.
	Supports EPOLLIN, EPOLLOUT, and EPOLLIN|EPOLLOUT simultaneously by
	registering one kevent per filter.
 */
class Epoll
{
	struct epoll_data_t
	{
		uint32_t _events; /* original event mask from epoll_event */
		void *_ptr;		  /* original ptr from epoll_event */
		int _filter;	  /* EVFILT_READ or EVFILT_WRITE */

		epoll_data_t(uint32_t events, void *ptr, int filter)
			: _events(events), _ptr(ptr), _filter(filter) {}
	};

public:
	int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
	{
		switch (op)
		{
			case EPOLL_CTL_ADD:
			{
				if (event == nullptr)
					return EINVAL;

				const uint32_t orig_events = event->events;
				const bool want_read  = (orig_events & EPOLLIN)  != 0;
				const bool want_write = (orig_events & EPOLLOUT) != 0;
				const bool edge_trig  = (orig_events & EPOLLET)  != 0;

				if (!want_read && !want_write)
				{
					logte("epoll_ctl(): neither EPOLLIN nor EPOLLOUT set");
					return EINVAL;
				}

				{
					std::lock_guard<decltype(_mutex)> lock(_mutex);
					if (_epoll_data[epfd].count(fd))
					{
						logte("socket %d has already been added to epoll %d", fd, epfd);
						return EINVAL;
					}
				}

				// Register one kevent per requested filter
				for (int filter : {EVFILT_READ, EVFILT_WRITE})
				{
					if (filter == EVFILT_READ  && !want_read)  continue;
					if (filter == EVFILT_WRITE && !want_write) continue;

					auto *data = new epoll_data_t(orig_events, event->data.ptr, filter);
					{
						std::lock_guard<decltype(_mutex)> lock(_mutex);
						_epoll_data[epfd][fd].push_back(data);
					}

					struct kevent ke{};
					uint16_t flags = EV_ADD;
					if (edge_trig) flags |= EV_CLEAR;
					EV_SET(&ke, fd, filter, flags, 0, 0, data);
					if (kevent(epfd, &ke, 1, nullptr, 0, nullptr) != 0)
						return -1;
				}
				return 0;
			}

			case EPOLL_CTL_DEL:
			{
				std::vector<epoll_data_t *> to_delete;
				{
					std::lock_guard<decltype(_mutex)> lock(_mutex);
					auto &epoll_fd_data = _epoll_data[epfd];
					const auto it = epoll_fd_data.find(fd);
					if (it == epoll_fd_data.end())
					{
						logte("socket %d has not been added to epoll %d", fd, epfd);
						return EINVAL;
					}
					to_delete = std::move(it->second);
					epoll_fd_data.erase(it);
				}

				int result = 0;
				for (auto *data : to_delete)
				{
					struct kevent ke{};
					EV_SET(&ke, fd, data->_filter, EV_DELETE, 0, 0, nullptr);
					if (kevent(epfd, &ke, 1, nullptr, 0, nullptr) != 0)
						result = -1;
					delete data;
				}
				return result;
			}

			default:
				return EINVAL;
		}
	}

	int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
	{
		std::vector<struct kevent> ke(maxevents);
		const timespec t{
			.tv_sec  = timeout / 1000,
			.tv_nsec = (timeout % 1000) * 1000 * 1000};
		int result = kevent(epfd, nullptr, 0, ke.data(), maxevents, timeout == -1 ? nullptr : &t);
		if (result > 0)
		{
			for (int i = 0; i < result; ++i)
			{
				const auto *data = static_cast<epoll_data_t *>(ke[i].udata);
				if (data == nullptr)
				{
					logte("kevent() returned a kevent structure with an empty udata field");
					exit(1);
				}
				events[i].data.ptr = data->_ptr;
				events[i].events   = 0;

				if (ke[i].filter == EVFILT_READ)
				{
					if (ke[i].flags & EV_EOF)
						events[i].events = (data->_events & EPOLLRDHUP) ? EPOLLRDHUP : EPOLLHUP;
					else
						events[i].events = EPOLLIN;
				}
				else if (ke[i].filter == EVFILT_WRITE)
				{
					if (ke[i].flags & EV_EOF)
						events[i].events = (data->_events & EPOLLHUP) ? EPOLLHUP : EPOLLERR;
					else
						events[i].events = EPOLLOUT;
				}
				else
				{
					logte("kevent() returned unexpected filter value %d", ke[i].filter);
					exit(1);
				}

				if (ke[i].flags & EV_ERROR)
					events[i].events |= EPOLLERR;
			}
		}
		return result;
	}

private:
	inline static std::mutex _mutex;
	inline static std::unordered_map<int, std::unordered_map<int, std::vector<epoll_data_t *>>> _epoll_data;
};

static Epoll epoll;

inline int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	return epoll.epoll_ctl(epfd, op, fd, event);
}

inline int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
	return epoll.epoll_wait(epfd, events, maxevents, timeout);
}
#endif	// IS_MACOS
