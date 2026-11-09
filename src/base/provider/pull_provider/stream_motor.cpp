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
	// The data of an epoll event: the stream ID in the low 32 bits and the low 32 bits of the registered socket's ID above it,
	// so an event of an earlier registration of the same stream is told apart
	static uint64_t MakeEventKey(uint32_t stream_id, ov::socket_id_t socket_id)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(socket_id.GetValue())) << 32) | stream_id;
	}

	static uint32_t StreamIdOfEventKey(uint64_t key)
	{
		return static_cast<uint32_t>(key);
	}

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

		if (event_socket != nullptr)
		{
			auto error = event_socket->DeleteFromExternalEpoll(_epoll_fd);

			if (error != nullptr)
			{
				logte("%s/%s(%u) Stream could not be deleted from the epoll: %s", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), error->What());
			}
		}

		_streams.erase(item);

		return true;
	}

	bool StreamMotor::AddStream(const std::shared_ptr<PullStream> &stream)
	{
		const bool by_epoll						 = (stream->GetProcessMediaEventTriggerMode() == PullStream::ProcessMediaEventTrigger::TRIGGER_EPOLL);
		std::shared_ptr<ov::Socket> event_socket = by_epoll ? stream->GetSocketForDetectingEvent() : nullptr;
		std::shared_ptr<ov::Error> error;

		{
			ov::LockGuard lock(_streams_map_guard);

			// A resumed stream may still be here, possibly with another socket
			RemoveEntry(stream->GetId());

			if (by_epoll)
			{
				if (event_socket == nullptr)
				{
					error = ov::Error::CreateError("StreamMotor", "Stream failed to provide the socket for event detection");
				}
				else
				{
					struct epoll_event event;
					event.events   = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
					event.data.u64 = MakeEventKey(stream->GetId(), event_socket->GetId());

					error		   = event_socket->AddToExternalEpoll(_epoll_fd, &event);
				}
			}

			if (error == nullptr)
			{
				_streams[stream->GetId()] = {stream, event_socket};
			}
		}

		if (error != nullptr)
		{
			logte("%s/%s(%u) Could not add the stream: %s", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), error->What());

			stream->Stop();
			return false;
		}

		logti("%s/%s(%u) stream has added to %u StreamMotor", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), GetId());

		return true;
	}

	bool StreamMotor::DelStream(const std::shared_ptr<PullStream> &stream)
	{
		return DelStream(stream, nullptr);
	}

	bool StreamMotor::DelStream(const std::shared_ptr<PullStream> &stream, const std::shared_ptr<ov::Socket> &event_socket)
	{
		{
			ov::LockGuard lock(_streams_map_guard);

			auto item = _streams.find(stream->GetId());

			// may be already deleted, or added again with another socket
			if ((item == _streams.end()) || ((event_socket != nullptr) && (item->second.event_socket != event_socket)))
			{
				return false;
			}

			RemoveEntry(stream->GetId());
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
				auto key = epoll_events[i].data.u64;
				auto events = epoll_events[i].events;

				std::shared_ptr<PullStream> stream;
				std::shared_ptr<ov::Socket> event_socket;

				{
					ov::SharedLockGuard lock(_streams_map_guard);
					auto stream_id = StreamIdOfEventKey(key);
					auto it = _streams.find(stream_id);
					if(it == _streams.end())
					{
						continue;
					}

					event_socket = it->second.event_socket;
					if ((event_socket == nullptr) || (MakeEventKey(stream_id, event_socket->GetId()) != key))
					{
						continue;
					}

					stream = it->second.stream;
				}

				if (OV_CHECK_FLAG(events, EPOLLHUP) || OV_CHECK_FLAG(events, EPOLLRDHUP))
				{
					logti("An error (%u) occurred while epoll_waiting the %s - %s/%s(%u) stream.", events, stream->GetApplicationTypeName(), stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId());
					DelStream(stream, event_socket);
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
							DelStream(stream, event_socket);
						}
					}
					else
					{
						DelStream(stream, event_socket);
					}
				}
				else
				{
					logti("An unexpected error (%u) occurred while epoll_waiting the %s - %s/%s(%u) stream.", events, stream->GetApplicationTypeName(), stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId());
					DelStream(stream, event_socket);
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
