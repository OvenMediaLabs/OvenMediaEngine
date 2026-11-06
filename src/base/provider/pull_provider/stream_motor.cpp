//==============================================================================
//
//  StreamMotor
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "stream_motor.h"

#include <base/ovsocket/socket.h>

#include "provider_private.h"

namespace pvd
{
	StreamMotor::StreamMotor(uint32_t id)
	{
		_id = id;
	}

	uint32_t StreamMotor::GetId()
	{
		return _id;
	}

	uint32_t StreamMotor::GetStreamCount()
	{
		ov::SharedLockGuard lock(_streams_map_guard);
		return _streams.size();
	}

	bool StreamMotor::Start()
	{
		// create epoll
		_epoll_fd = epoll_create1(0);
		if(_epoll_fd == -1)
		{
			logte("Cannot create EPOLL fd : %d", _id);
			return false;
		}

		_stop_thread_flag = false;
		_thread = std::thread(&StreamMotor::WorkerThread, this);
		pthread_setname_np(_thread.native_handle(), "StreamMotor");

		return true;
	}

	bool StreamMotor::Stop()
	{
		_stop_thread_flag = true;
		if(_thread.joinable())
		{
			_thread.join();
		}

		// Taken out under the lock and stopped outside it, so no lock is held while a stream stops
		decltype(_streams) streams;
		{
			ov::LockGuard lock(_streams_map_guard);

			// Reset once closed, so a second `Stop()` never closes a number that another file has taken since
			if (_epoll_fd != -1)
			{
				close(_epoll_fd);
				_epoll_fd = -1;
			}

			std::swap(streams, _streams);
		}

		for (const auto &[id, entry] : streams)
		{
			entry.stream->Stop();
		}

		return true;
	}

	bool StreamMotor::RemoveEntry(uint32_t stream_id)
	{
		auto item = _streams.find(stream_id);

		if (item == _streams.end())
		{
			return false;
		}

		const auto &[stream, event_socket] = item->second;

		if ((event_socket != nullptr) && (event_socket->DeleteFromExternalEpoll(_epoll_fd) == false))
		{
			logte("%s/%s(%u) Stream could not be deleted to the epoll (err : %d)", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), errno);
		}

		_streams.erase(item);

		return true;
	}

	bool StreamMotor::AddStream(const std::shared_ptr<PullStream> &stream)
	{
		const bool by_epoll						 = (stream->GetProcessMediaEventTriggerMode() == PullStream::ProcessMediaEventTrigger::TRIGGER_EPOLL);
		std::shared_ptr<ov::Socket> event_socket = by_epoll ? stream->GetSocketForDetectingEvent() : nullptr;

		if (by_epoll && (event_socket == nullptr))
		{
			logte("Failed to add stream : %s/%s(%u) Stream failed to provide the socket for event detection", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId());
		}

		bool added = (by_epoll == false);

		{
			ov::LockGuard lock(_streams_map_guard);

			// A resumed stream may still be here, possibly with another socket
			RemoveEntry(stream->GetId());

			if (event_socket != nullptr)
			{
				struct epoll_event event;
				event.events   = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
				event.data.u32 = stream->GetId();

				added		   = event_socket->AddToExternalEpoll(_epoll_fd, &event);

				if (added == false)
				{
					logte("%s/%s(%u) Stream could not be added to the epoll (err : %d)", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), errno);
				}
			}

			if (added)
			{
				_streams[stream->GetId()] = {stream, event_socket};
			}
		}

		if (added == false)
		{
			stream->Stop();
			return false;
		}

		logti("%s/%s(%u) stream has added to %u StreamMotor", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), GetId());

		return true;
	}

	bool StreamMotor::DelStream(const std::shared_ptr<PullStream> &stream)
	{
		{
			ov::LockGuard lock(_streams_map_guard);

			if (RemoveEntry(stream->GetId()) == false)
			{
				// may be already deleted
				return false;
			}
		}

		logti("%s/%s(%u) stream has deleted from %u StreamMotor", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), GetId());

		stream->Stop();

		return true;
	}

	void StreamMotor::WorkerThread()
	{
		ov::logger::ThreadHelper thread_helper;

		while(true)
		{
			struct epoll_event epoll_events[MAX_EPOLL_EVENTS];

			int event_count = epoll_wait(_epoll_fd, epoll_events, MAX_EPOLL_EVENTS, EPOLL_TIMEOUT_MSEC);

			if(_stop_thread_flag)
			{
				break;
			}

			if(event_count < 0)
			{
				if(errno == EINTR)
				{
					continue;
				}
				else
				{
					logtc("%d StreamMotor terminated : epoll_wait error (errno : %d)", _id, errno);
					return;
				}
			}

			for(int i=0; i<event_count; i++)
			{
				auto stream_id = epoll_events[i].data.u32;
				auto events = epoll_events[i].events;

				std::shared_ptr<PullStream> stream;

				{
					ov::SharedLockGuard lock(_streams_map_guard);
					auto it = _streams.find(stream_id);
					if(it == _streams.end())
					{
						continue;
					}

					stream = it->second.stream;
				}

				if (OV_CHECK_FLAG(events, EPOLLHUP) || OV_CHECK_FLAG(events, EPOLLRDHUP))
				{
					logti("An error (%u) occurred while epoll_waiting the %s - %s/%s(%u) stream.", events, stream->GetApplicationTypeName(), stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId());
					DelStream(stream);
				}
				else if(OV_CHECK_FLAG(events, EPOLLIN))
				{
					if(stream->GetState() == Stream::State::PLAYING)
					{
						auto result = stream->ProcessMediaPacket();
						if(result == PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS)
						{
						}
						else if(result == PullStream::ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN)
						{
						}
						else
						{
							DelStream(stream);
						}
					}
					else
					{
						DelStream(stream);
					}
				}
				else
				{
					logti("An unexpected error (%u) occurred while epoll_waiting the %s - %s/%s(%u) stream.", events, stream->GetApplicationTypeName(), stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId());
					DelStream(stream);
				}
			}

			ov::SharedLockGuard lock(_streams_map_guard);
			for (const auto &[stream_id, entry] : _streams)
			{
				const auto &stream = entry.stream;

				if (stream->GetProcessMediaEventTriggerMode() != PullStream::ProcessMediaEventTrigger::TRIGGER_INTERVAL)
					continue;

				if (stream->GetState() == Stream::State::PLAYING)
				{
					auto result = stream->ProcessMediaPacket();
					if (result == PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS ||
						result == PullStream::ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN)
					{
					}
					else
					{
						stream->Stop();
					}
				}
				else
				{
					stream->Stop();
				}
			}
		}
	}
}  // namespace pvd
