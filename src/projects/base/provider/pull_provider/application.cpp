//==============================================================================
//
//  PullProvider Base Class 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#include "provider.h"
#include "application.h"
#include "stream.h"
#include "stream_props.h"
#include "provider_private.h"

namespace pvd
{
	PullApplication::PullApplication(const std::shared_ptr<PullProvider> &provider, const info::Application &application_info)
		: Application(provider, application_info)
	{

	}

	PullApplication::~PullApplication()
	{

	}

	bool PullApplication::Start()
	{
		_stop_collector_thread_flag = false;
		_collector_thread = std::thread(&PullApplication::WhiteElephantStreamCollector, this);
		pthread_setname_np(_collector_thread.native_handle(), "StreamCollector");
		return Application::Start();
	}

	bool PullApplication::Stop()
	{
		if(GetState() == ApplicationState::Stopped)
		{
			return true;
		}

		_stop_collector_thread_flag = true;
		if(_collector_thread.joinable())
		{
			_collector_thread.join();
		}

		return Application::Stop();
	}

	// It works only with pull provider
	void PullApplication::WhiteElephantStreamCollector()
	{
		ov::logger::ThreadHelper thread_helper;

		auto global_no_input_timeout_ms = GetHostInfo().GetOrigins().GetProperties().GetNoInputFailoverTimeout(); 
		auto global_unused_stream_timeout_ms = GetHostInfo().GetOrigins().GetProperties().GetUnusedStreamDeletionTimeout();
		auto global_failback_timeout_ms = GetHostInfo().GetOrigins().GetProperties().GetStreamFailbackTimeout();	
		
		constexpr int64_t idle_wait_time_ms = 100; 
		while (!_stop_collector_thread_flag)
		{
			auto streams = GetStreams();
			for (auto const &x : streams)
			{
				auto stream = std::dynamic_pointer_cast<PullStream>(x.second);

				if (stream->GetState() == Stream::State::STOPPED || stream->GetState() == Stream::State::ERROR)
				{
					// Retry
					ResumeStream(stream);
				}
				else if (stream->GetState() == Stream::State::TERMINATED)
				{
					// Tried several times, but if unsuccessful, delete it completely.
					DeleteStream(stream);
				}
				else
				{
					auto current = std::chrono::high_resolution_clock::now();

					// Default Properties of PullStream
					auto is_persistent = false;
					auto is_failback = false;
					int64_t no_input_timeout_ms = global_no_input_timeout_ms;
					int64_t unused_stream_timeout_ms = global_unused_stream_timeout_ms;
					int64_t failback_timeout_ms = global_failback_timeout_ms;

					auto props = stream->GetProperties();
					if (props)
					{
						is_persistent = props->IsPersistent();
						is_failback = props->IsFailback();

						if (props->GetNoInputFailoverTimeout() > 0)
						{
							no_input_timeout_ms = props->GetNoInputFailoverTimeout();
						}

						if (props->GetUnusedStreamDeletionTimeout() > 0)
						{
							unused_stream_timeout_ms = props->GetUnusedStreamDeletionTimeout();
						}

						if (props->GetStreamFailbackTimeoutMSec() > 0)
						{
							failback_timeout_ms = props->GetStreamFailbackTimeoutMSec();
						}
					
						// If Failback is enabled, try to connect periodically to see if the Primary URL is available.
						if(is_failback) 
						{
							auto elapsed_time_from_last_failback_check = std::chrono::duration_cast<std::chrono::milliseconds>(current - props->GetLastFailbackCheckTime()).count();

							if((elapsed_time_from_last_failback_check > failback_timeout_ms) && (!stream->IsCurrPrimaryURL()))
							{
								auto failback_url = stream->GetPrimaryURL()->ToUrlString(true);
							
								logtt("%s/%s(%u) Attempt to connect to verify that the primary stream is available. url(%s)", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId(), failback_url.CStr());
							
								auto ping_props = std::make_shared<pvd::PullStreamProperties>();
								ping_props->SetRetryCount(0);
								auto ping = CreateStream(0, "_ping_for_failback_", {failback_url}, ping_props);
								if (ping)
								{
									auto state = ping->GetState() ;
									ping->Stop();

									if((state == pvd::Stream::State::PLAYING) || (state == pvd::Stream::State::DESCRIBED))
									{
										// Stop the current stream and switch to the Primary URL.
										stream->Stop();
										stream->ResetUrlIndex();
										ResumeStream(stream);
									}
									
								}
								else 
								{
									logtt("%s/%s(%u) primary stream is not available.", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId());
								}

								props->UpdateFailbackCheckTime();
							}
						}
					}

					// Check if there are streams have no any viewers
					auto stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(stream));
					if(stream_metrics != nullptr)
					{
						auto elapsed_time_from_last_sent = std::chrono::duration_cast<std::chrono::milliseconds>(current - stream_metrics->GetLastSentTime()).count();
						auto elapsed_time_from_last_recv = std::chrono::duration_cast<std::chrono::milliseconds>(current - stream_metrics->GetLastRecvTime()).count();

						if((elapsed_time_from_last_sent > unused_stream_timeout_ms) && (!is_persistent))
						{
							logtw("%s/%s(%u) stream will be deleted because it hasn't been used for %u milliseconds", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId(), elapsed_time_from_last_sent);
							DeleteStream(stream);
						}
						// The stream type is pull stream, if packets do NOT arrive for more than 3 seconds, it is a seriously warning situation
						else if(elapsed_time_from_last_recv > no_input_timeout_ms && (!is_persistent))
						{
							logtw("Stop stream %s/%s(%u) : there are no incoming packets. %d milliseconds have elapsed since the last packet was received.",
								  stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId(), elapsed_time_from_last_recv);

							// When the stream is stopped, it tries to reconnect using the next url.
							stream->Stop();
						}
					}
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(idle_wait_time_ms));
		}
	}

	uint32_t PullApplication::GetStreamMotorId(const std::shared_ptr<PullStream> &stream)
	{
		uint32_t hash = stream->GetId() % MAX_APPLICATION_STREAM_MOTOR_COUNT;

		return hash;
	}

	bool PullApplication::AddStreamToMotorInternal(const std::shared_ptr<PullStream> &stream)
	{
		std::unique_lock<std::shared_mutex> lock(_stream_motors_guard);

		std::shared_ptr<StreamMotor> motor = nullptr;
		auto motor_id = GetStreamMotorId(stream);
		auto it = _stream_motors.find(motor_id);
		if (it == _stream_motors.end())
		{
			motor = std::make_shared<StreamMotor>(motor_id);
			motor->Start();

			_stream_motors.emplace(motor_id, motor);
			logti("%s application has created %u StreamMotor", stream->GetApplicationInfo().GetVHostAppName().CStr(), motor_id);
		}
		else
		{
			motor = it->second;
		}

		motor->AddStream(stream);
		logti("%s/%s(%u) stream has been added to StreamMotor %u", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId(), motor_id);
		return true;
	}

	bool PullApplication::DeleteStreamFromMotorInternal(const std::shared_ptr<PullStream> &stream)
	{
		std::unique_lock<std::shared_mutex> lock(_stream_motors_guard);

		std::shared_ptr<StreamMotor> motor = nullptr;
		auto motor_id = GetStreamMotorId(stream);
		auto it = _stream_motors.find(motor_id);
		if (it == _stream_motors.end())
		{
			logte("Could not find StreamMotor to remove stream : %s/%s(%u)", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId());
			return false;
		}
		else
		{
			motor = it->second;
		}

		motor->DelStream(stream);
		logti("%s/%s(%u) stream has been deleted from stream motor %u", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId(), motor_id);

		if(motor->GetStreamCount() == 0)
		{
			motor->Stop();
			auto motor_id = GetStreamMotorId(stream);
			_stream_motors.erase(motor_id);

			logti("%s application has deleted %u stream motor", stream->GetApplicationInfo().GetVHostAppName().CStr(), motor_id);
		}

		return true;
	}

	std::shared_ptr<pvd::Stream> PullApplication::CreateStream(const ov::String &stream_name, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties)
	{
		auto global_retry_count = GetHostInfo().GetOrigins().GetProperties().GetRetryCount();
		if (properties->GetRetryCount() <= 0)
		{
			properties->SetRetryCount(global_retry_count);
		}

		auto stream = CreateStream(pvd::Application::IssueUniqueStreamId(), stream_name, url_list, properties);
		if(stream == nullptr)
		{
			return nullptr;
		}

		if (AddStream(stream) == false)
		{
			logte("Could not add stream : %s/%s(%u)", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId());
			return nullptr;
		}

		if (AddStreamToMotorInternal(stream) == false)
		{
			logte("Could not add stream to motor : %s/%s(%u)", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId());
			DeleteStream(stream);
			return nullptr;
		}

		return stream;
	}

	bool PullApplication::ResumeStream(const std::shared_ptr<Stream> &stream)
	{
		auto pull_stream = std::dynamic_pointer_cast<PullStream>(stream);
		if(pull_stream == nullptr)
		{
			return false;
		}

		if(pull_stream->Resume() == false)
		{
			return false;
		}

		if (AddStreamToMotorInternal(pull_stream) == false)
		{
			logte("Could not add stream to motor : %s/%s(%u)", stream->GetApplicationInfo().GetVHostAppName().CStr(), stream->GetName().CStr(), stream->GetId());
			DeleteStream(stream);
			return false;
		}

		NotifyStreamUpdated(pull_stream);

		return true;
	}

	bool PullApplication::DeleteStream(const std::shared_ptr<Stream> &stream)
	{
		DeleteStreamFromMotorInternal(std::dynamic_pointer_cast<PullStream>(stream));
		return Application::DeleteStream(stream);
	}

	bool PullApplication::DeleteAllStreams()
	{
		for(const auto &x : _stream_motors)
		{
			auto motor = x.second;
			motor->Stop();
		}

		_stream_motors.clear();

		return Application::DeleteAllStreams();
	}
}