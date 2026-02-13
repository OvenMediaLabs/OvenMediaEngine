//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "semaphore.h"
#include "queue.h"
#include <any>

namespace ov
{
	struct CommonMessage
	{
		uint32_t	_code;
		std::any	_message;
	};

	template <typename T>
	class MessageThreadObserver : public EnableSharedFromThis<MessageThreadObserver<T>>
	{
	public:
		virtual void OnMessage(const T &message) = 0;
	};

	template <typename T>
	class MessageThread
	{
	public:
		bool Start(const std::shared_ptr<MessageThreadObserver<T>> &observer)
		{
			_observer = observer;
			_run_thread = true;
			_thread = std::thread(&MessageThread::Postman, this);
			pthread_setname_np(_thread.native_handle(), "MessageThread");

			return true;
		}

		bool Stop()
		{
			_run_thread = false;
			_event.Stop();
			if(_thread.joinable())
			{
				_thread.join();
			}

			_observer.reset();

			return true;
		}

		bool SendMessage(const T &message)
		{
			_observer->OnMessage(message);
			return true;
		}

		bool PostMessage(const T &message)
		{
			_message_queue.Enqueue(message);
			_event.Notify();
			return true;
		}

	private:
		void Postman()
		{
			logger::ThreadHelper thread_helper;

			while(_run_thread)
			{
				_event.Wait();	
				auto item = _message_queue.Dequeue(0);
				if(item.has_value() == false)
				{
					continue;
				}

				_observer->OnMessage(item.value());
			}
		}

		Semaphore		_event;
		bool			_run_thread = false;
		std::thread 	_thread;
		Queue<T>		_message_queue;
		std::shared_ptr<MessageThreadObserver<T>>	_observer;
	};
}