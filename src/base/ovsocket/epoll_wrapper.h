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
	This is an experimental wrapper class for epoll functionality on macOS
 */
class Epoll
{
	struct epoll_data_t
	{
		uint32_t _events; /* original event mask from epoll_event */
		void *_ptr;		  /* original ptr from epoll_event */
		int _filter;	  /* computed filter */

		epoll_data_t(uint32_t events, void *ptr, int filter)
			: _events(events),
			  _ptr(ptr),
			  _filter(filter)
		{
		}
	};

public:
	int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
	{
		// EV_DELETE needs the filter flags upon deletion so store them locally, since epoll does not
		// need this thus the original event will not be passed in with EPOLL_CTL_DEL
		epoll_data_t *epoll_data = nullptr;
		struct kevent ke
		{
		};
		switch (op)
		{
			case EPOLL_CTL_ADD:
				if (event == nullptr)
				{
					return EINVAL;
				}
				if ((event->events & (EPOLLIN | EPOLLOUT)) == (EPOLLIN | EPOLLOUT))
				{
					// This wrapper currently does not support EPOLLIN | EPOLLOUT, but there is no such use case so far,
					// this is not trivial since two explicit kevent structures need to be added in this case and when epoll_wait
					// needs to know how to combine the flags back together
					logte("epoll_ctl() currently does not support EPOLLIN | EPOLLOUT");
					return EINVAL;
				}
				{
					const auto events = event->events;
					ke.flags = EV_ADD;
					if (event->events & EPOLLIN)
					{
						if (event->events & EPOLLET)
						{
							ke.flags |= EV_CLEAR;
						}
						ke.filter = EVFILT_READ;
						event->events &= ~EPOLLIN;
					}
					else if (event->events & EPOLLOUT)
					{
						if (event->events & EPOLLET)
						{
							ke.flags |= EV_CLEAR;
						}
						ke.filter = EVFILT_WRITE;
						event->events &= ~EPOLLOUT;
					}
					if (event->events & EPOLLET)
					{
						event->events &= ~EPOLLET;
					}
					if (event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
					{
						event->events &= ~(EPOLLERR | EPOLLHUP | EPOLLRDHUP);
					}
					if (event->events != 0)
					{
						// Unhandled epoll flags provided
						logte("unhandled flags %u passed to epoll_ctl", event->events);
						return EINVAL;
					}
					std::unique_lock<decltype(_mutex)> lock(_mutex);
					auto &epoll_fd_data = _epoll_data[epfd];
					if (epoll_fd_data.find(fd) != epoll_fd_data.end())
					{
						lock.unlock();
						logte("socket %d has already been added to epoll %d", fd, epfd);
						return EINVAL;
					}
					epoll_data = &_epoll_data[epfd].emplace(std::piecewise_construct, std::forward_as_tuple(fd), std::forward_as_tuple(events, event->data.ptr, ke.filter)).first->second;
				}
				break;
			case EPOLL_CTL_DEL:
				ke.flags = EV_DELETE;
				{
					std::lock_guard<decltype(_mutex)> lock(_mutex);
					auto &epoll_fd_data = _epoll_data[epfd];
					const auto it = epoll_fd_data.find(fd);
					if (it != epoll_fd_data.end())
					{
						ke.filter = it->second._filter;
						epoll_fd_data.erase(fd);
					}
					else
					{
						logte("socket %d has not been added to epoll %d", fd, epfd);
						return EINVAL;
					}
				}
				break;
		}
		EV_SET(&ke, fd, ke.filter, ke.flags, 0, 0, epoll_data);
		int result = kevent(epfd, &ke, 1, nullptr, 0, nullptr);
		return result;
	}

	int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
	{
		struct kevent ke[maxevents];
		memset(&ke, 0, sizeof(ke));
		const timespec t{
			.tv_sec = timeout / 1000,
			.tv_nsec = (timeout % 1000) * 1000 * 1000};
		int result = kevent(epfd, nullptr, 0, ke, maxevents, timeout == -1 ? nullptr : &t);
		if (result > 0)
		{
			for (int event_index = 0; event_index < result; ++event_index)
			{
				const auto *epoll_data = static_cast<epoll_data_t *>(ke[event_index].udata);
				if (epoll_data == nullptr)
				{
					// This should never happen, but must be at least gracefully handled
					logte("kevent() returned a kevent structure with an empty udata field");
					exit(1);
				}
				events[event_index].data.ptr = epoll_data->_ptr;
				events[event_index].events = 0;
				if (ke[event_index].filter == EVFILT_READ)
				{
					if ((ke[event_index].flags & EV_EOF))
					{
						if (epoll_data->_events & EPOLLRDHUP)
						{
							events[event_index].events = EPOLLRDHUP;
						}
					}
					else
					{
						events[event_index].events = EPOLLIN;
					}
				}
				else if (ke[event_index].filter == EVFILT_WRITE)
				{
					if ((ke[event_index].flags & EV_EOF))
					{
						if (epoll_data->_events & EPOLLHUP)
						{
							events[event_index].events = EPOLLHUP;
						}
					}
					else
					{
						events[event_index].events = EPOLLOUT;
					}
				}
				else
				{
					// TODO: unexpected filter value, currently just die, since returning something here
					// might mess up the caller
					logte("kevent() returned unexpected filter value %d", ke[event_index].filter);
					exit(1);
				}
				if (ke[event_index].flags & EV_ERROR)
				{
					// TODO: handle EV_ERROR
				}
			}
		}
		return result;
	}

private:
	static std::mutex _mutex;
	static std::unordered_map<int, std::unordered_map<int, epoll_data_t>> _epoll_data;
};

std::mutex Epoll::_mutex;
std::unordered_map<int, std::unordered_map<int, Epoll::epoll_data_t>> Epoll::_epoll_data;

static Epoll epoll;

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	return epoll.epoll_ctl(epfd, op, fd, event);
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
	return epoll.epoll_wait(epfd, events, maxevents, timeout);
}
#endif	// IS_MACOS
