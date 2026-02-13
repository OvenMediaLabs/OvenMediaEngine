//==============================================================================
//
//  StreamMotor
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "stream_motor.h"
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
		std::shared_lock<std::shared_mutex> lock(_streams_map_guard);
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
		close(_epoll_fd);
		if(_thread.joinable())
		{
			_thread.join();
		}

		//STOP AND REMOVE ALL STREAM (NEXT)
		for(const auto &x : _streams)
		{
			auto stream = x.second;
			stream->Stop();
		}

		_streams.clear();

		return true;
	}

	bool StreamMotor::AddStreamToEpoll(const std::shared_ptr<PullStream> &stream)
	{
		int stream_fd = stream->GetFileDescriptorForDetectingEvent();
		if(stream_fd == -1)
		{
			logte("Failed to add stream : %s/%s(%u) Stream failed to provide the file description for event detection", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId());
			return false;
		}

		struct epoll_event event;
		event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
		event.data.u32 = stream->GetId();

		//TODO(Getroot): epoll_ctl and epoll_wait are thread-safe so it doesn't need to be protected. (right?, check carefully again!)
		int result = epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, stream_fd, &event);
		if(result == -1)
		{
			logte("%s/%s(%u) Stream could not be added to the epoll (err : %d)", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), errno);
			return false;
		}

		return true;
	}

	bool StreamMotor::DelStreamFromEpoll(const std::shared_ptr<PullStream> &stream)
	{
		int stream_fd = stream->GetFileDescriptorForDetectingEvent();
		if(stream_fd == -1)
		{
			logte("Failed to delete stream : %s/%s(%u) Stream failed to provide the file description for event detection", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId());
			return false;
		}

		//TODO(Getroot): epoll_ctl and epoll_wait are thread-safe so it doesn't need to be protected. (right?, check carefully again!)
		int result = epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, stream_fd, nullptr);
		if(result == -1)
		{
			if(errno == ENOENT)
			{
				return true;
			}
			logte("%s/%s(%u) Stream could not be deleted to the epoll (err : %d)", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), result);
			return false;
		}

		return true;
	}

	bool StreamMotor::AddStream(const std::shared_ptr<PullStream> &stream)
	{
		std::unique_lock<std::shared_mutex> lock(_streams_map_guard);
		_streams[stream->GetId()] = stream;
		lock.unlock();

		switch (stream->GetProcessMediaEventTriggerMode())
		{
			case PullStream::ProcessMediaEventTrigger::TRIGGER_EPOLL:
				if (!AddStreamToEpoll(stream))
				{
					DelStream(stream);
					return false;
				}
				break;
			case PullStream::ProcessMediaEventTrigger::TRIGGER_INTERVAL:
			default:
				break;
		}

		logti("%s/%s(%u) stream has added to %u StreamMotor", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), GetId());

		return true;
	}

	bool StreamMotor::DelStream(const std::shared_ptr<PullStream> &stream)
	{
		std::unique_lock<std::shared_mutex> lock(_streams_map_guard);
		if(_streams.find(stream->GetId()) == _streams.end())
		{
			// may be already deleted
			return false;
		}
		_streams.erase(stream->GetId());
		lock.unlock();

		logti("%s/%s(%u) stream has deleted from %u StreamMotor", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId(), GetId());

		switch (stream->GetProcessMediaEventTriggerMode())
		{
			case PullStream::ProcessMediaEventTrigger::TRIGGER_EPOLL:
				DelStreamFromEpoll(stream);
				break;
			case PullStream::ProcessMediaEventTrigger::TRIGGER_INTERVAL:
			default:
				break;
		}

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

				std::shared_lock<std::shared_mutex> stream_lock(_streams_map_guard);
				auto it = _streams.find(stream_id);
				if(it == _streams.end())
				{
					continue;
				}

				auto stream = it->second;
				stream_lock.unlock();

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

			std::shared_lock<std::shared_mutex> stream_lock(_streams_map_guard);
			for (const auto &[stream_id, stream] : _streams)
			{

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
			stream_lock.unlock();
		}
	}
}  // namespace pvd