//==============================================================================
//
//  PullProvider Base Class 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"
#include "base/info/stream.h"
#include "base/provider/stream.h"
#include "monitoring/monitoring.h"

namespace pvd
{
	class Application;
	class PullStreamProperties;
	
	class PullStream : public Stream
	{
	public:
		enum class ProcessMediaResult
		{
			PROCESS_MEDIA_SUCCESS,
			PROCESS_MEDIA_FAILURE,
			PROCESS_MEDIA_FINISH,
			PROCESS_MEDIA_TRY_AGAIN
		};

		// PullStream API
		bool Start() override;
		bool Stop() override;
		bool Resume(); // Resume with another URL

		// Defines the event detection method to process media packets in Pull Stream. 
		// There are EPOLL event method by socket and INTERVAL event method called at regular time.	
		enum class ProcessMediaEventTrigger {
			TRIGGER_EPOLL,
			TRIGGER_INTERVAL
		};
		virtual ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() = 0;

		// It is used to detect event by StreamMotor and then StreamMotor calls ProcessMediaPacket
		// Internally it is used for epoll
		virtual int GetFileDescriptorForDetectingEvent() = 0;

		// If this stream belongs to the Pull provider, 
		// this function is called periodically by the StreamMotor of application. 
		// Media data has to be processed here.
		virtual ProcessMediaResult ProcessMediaPacket() = 0;

	protected:
		PullStream(const std::shared_ptr<pvd::Application> &application, const info::Stream &stream_info, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties = nullptr);

		virtual bool StartStream(const std::shared_ptr<const ov::Url> &url) = 0; // Start
		virtual bool RestartStream(const std::shared_ptr<const ov::Url> &url) = 0; // Failover
		virtual bool StopStream() = 0; // Stop

		DirectionType GetDirectionType() override
		{
			return DirectionType::PULL;
		}

	private:
		uint32_t	_restart_count = 0;
		std::vector<std::shared_ptr<const ov::Url>> _url_list;
		int _curr_url_index = 0;

		std::shared_ptr<pvd::PullStreamProperties> _properties;

		// It can be called by multiple thread
		std::mutex _start_stop_stream_lock;

	public:
		const std::shared_ptr<const ov::Url> GetNextURL();
		const std::shared_ptr<const ov::Url> GetPrimaryURL();
		void ResetUrlIndex();
		bool IsCurrPrimaryURL();

		std::shared_ptr<pvd::PullStreamProperties> GetProperties();
	};
}