//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "push_application.h"

#include "push_private.h"
#include "push_publisher.h"
#include "push_stream.h"

#define PUSH_PUBLISHER_ERROR_DOMAIN "PushPublisher"

namespace pub
{
	std::shared_ptr<PushApplication> PushApplication::Create(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info)
	{
		auto application = std::make_shared<PushApplication>(publisher, application_info);
		if (application->Start() == false)
		{
			return nullptr;
		}
		return application;
	}

	PushApplication::PushApplication(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info)
		: pub::Application(publisher, application_info),
		  _session_control_stop_thread_flag(true)
	{
	}

	PushApplication::~PushApplication()
	{
		Stop();
		logtt("PushApplication(%d) has been terminated finally", GetId());
	}

	bool PushApplication::Start()
	{
		auto config_publishers = GetConfig().GetPublishers();
		auto application_enabled = config_publishers.GetPushPublisher().IsParsed() 
									// Compatible with the old configuration
									|| config_publishers.GetRtmpPushPublisher().IsParsed() 
									|| config_publishers.GetSrtPushPublisher().IsParsed() 
									|| config_publishers.GetMpegtsPushPublisher().IsParsed();

		if (application_enabled == false)
		{
			logtt("%s PushApplication is disabled", GetVHostAppName().CStr());
			return false;
		}

		_session_control_stop_thread_flag = false;
		_session_contol_thread = std::thread(&PushApplication::SessionControlThread, this);
		pthread_setname_np(_session_contol_thread.native_handle(), "PushSessionCtrl");

		return pub::Application::Start();
	}

	bool PushApplication::Stop()
	{
		if (_session_control_stop_thread_flag == false)
		{
			_session_control_stop_thread_flag = true;
			if (_session_contol_thread.joinable())
			{
				_session_contol_thread.join();
			}
		}

		return pub::Application::Stop();
	}

	std::shared_ptr<pub::Stream> PushApplication::CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count)
	{
		// Automatically create a Push using StreamMap.
		auto publisher_config = GetConfig().GetPublishers().GetPushPublisher();
		auto stream_map = publisher_config.GetStreamMap();
		if(stream_map.IsEnabled() == true)
		{
			auto pushes = GetPushesByStreamMap(stream_map.GetPath(), info);
			for(auto &push : pushes)
			{
				StartPush(push, true);
			}
		}

		return PushStream::Create(GetSharedPtrAs<pub::Application>(), *info);
	}

	bool PushApplication::DeleteStream(const std::shared_ptr<info::Stream> &info)
	{
		auto stream = std::static_pointer_cast<PushStream>(GetStream(info->GetId()));
		if (stream == nullptr)
		{
			logte("PushApplication::Delete stream failed. Cannot find stream (%s)", info->GetName().CStr());
			return false;
		}

		// Remove the Push automatically created by StreamMap
		auto pushes = GetPushesByStreamName(info->GetName());
		for (auto &push : pushes)
		{
			if(push->IsByConfig() == false)
			{
				continue;
			}
			StopPush(push);
		}

		logtt("%s/%s stream has been deleted", GetVHostAppName().CStr(), stream->GetName().CStr());

		return true;
	}

	std::shared_ptr<info::Push> PushApplication::GetPushById(ov::String id)
	{
		std::shared_lock<std::shared_mutex> lock(_push_map_mutex);
		auto it = _pushes.find(id);
		if (it == _pushes.end())
		{
			return nullptr;
		}

		return it->second;
	}

	std::shared_ptr<ov::Error> PushApplication::StartPush(const std::shared_ptr<info::Push> &push, bool is_config)
	{
		ov::String error_message;

		if (Validate(push, error_message) == false)
		{
			return ov::Error::CreateError(PUSH_PUBLISHER_ERROR_DOMAIN, ErrorCode::FailureInvalidParameter, error_message);
		}

		// Validation check for duplicate id
		if (GetPushById(push->GetId()) != nullptr)
		{
			error_message = "Duplicate ID";
			return ov::Error::CreateError(PUSH_PUBLISHER_ERROR_DOMAIN, ErrorCode::FailureDuplicateKey, error_message);
		}

		push->SetEnable(true);
		push->SetRemove(false);
		push->SetByConfig(is_config);
		auto connection_timeout = GetConfig().GetPublishers().GetPushPublisher().GetConnectionTimeoutMs();
		auto send_timeout = GetConfig().GetPublishers().GetPushPublisher().GetSendTimeoutMs();
		if(connection_timeout > 0)
		{
			push->SetConnectionTimeout(connection_timeout);
		}
		if(send_timeout > 0)
		{
			push->SetSendTimeout(send_timeout);
		}

		std::unique_lock<std::shared_mutex> lock(_push_map_mutex);
		_pushes[push->GetId()] = push;

		return ov::Error::CreateError(PUSH_PUBLISHER_ERROR_DOMAIN, ErrorCode::Success, "Success");
	}

	std::shared_ptr<ov::Error> PushApplication::StopPush(const std::shared_ptr<info::Push> &push)
	{
		if (push->GetId().IsEmpty() == true)
		{
			ov::String error_message = "There is no required parameter [";

			if (push->GetId().IsEmpty() == true)
			{
				error_message += " id";
			}

			error_message += "]";

			return ov::Error::CreateError(PUSH_PUBLISHER_ERROR_DOMAIN, ErrorCode::FailureInvalidParameter, error_message);
		}

		auto push_info = GetPushById(push->GetId());
		if (push_info == nullptr)
		{
			ov::String error_message = ov::String::FormatString("There is no push information related to the ID [%s]", push->GetId().CStr());

			return ov::Error::CreateError(PUSH_PUBLISHER_ERROR_DOMAIN, ErrorCode::FailureNotExist, error_message);
		}

		push_info->SetEnable(false);
		push_info->SetRemove(true);

		return ov::Error::CreateError(PUSH_PUBLISHER_ERROR_DOMAIN, ErrorCode::Success, "Success");
	}

	std::vector<std::shared_ptr<info::Push>> PushApplication::GetPushesByStreamName(const ov::String streamName)
	{
		std::shared_lock<std::shared_mutex> lock(_push_map_mutex);
		std::vector<std::shared_ptr<info::Push>> results;
		for (auto &[id, push_info] : _pushes)
		{
			if (push_info->GetStreamName() != streamName)
				continue;

			results.push_back(push_info);
		}

		return results;
	}

	std::shared_ptr<ov::Error> PushApplication::GetPushes(const std::shared_ptr<info::Push> push, std::vector<std::shared_ptr<info::Push>> &results)
	{
		std::shared_lock<std::shared_mutex> lock(_push_map_mutex);

		for (auto &[id, push_info] : _pushes)
		{
			if (!push->GetId().IsEmpty() && push->GetId() != id)
				continue;

			results.push_back(push_info);
		}

		return ov::Error::CreateError(PUSH_PUBLISHER_ERROR_DOMAIN, ErrorCode::Success, "Success");
	}

	void PushApplication::StartPushInternal(const std::shared_ptr<info::Push> &push, std::shared_ptr<pub::Session> session)
	{
		// Check the status of the session.
		auto prev_session_state = session->GetState();

		switch (prev_session_state)
		{
			// State of disconnected and ready to connect
			case pub::Session::SessionState::Ready:
				logti("Push started. %s connectionTimeout(%d) sendTimeout(%d)", push->GetInfoString().CStr(), push->GetConnectionTimeout(), push->GetSendTimeout());
				session->Start();			
				break;
			// State of stopped
			case pub::Session::SessionState::Stopped:
				[[fallthrough]];
			// State of failed (connection refused, disconnected)
			case pub::Session::SessionState::Error:
				logti("Push restarted. %s connectionTimeout(%d) sendTimeout(%d)", push->GetInfoString().CStr(), push->GetConnectionTimeout(), push->GetSendTimeout());
				session->Start();
				break;
			// State of Started
			case pub::Session::SessionState::Started:
				[[fallthrough]];
			// State of Stopping
			case pub::Session::SessionState::Stopping:
				break;
		}

		auto session_state = session->GetState();
		if (prev_session_state != session_state)
		{
			logtt("Changed push state. (%d - %d)", ov::ToUnderlyingType(prev_session_state), ov::ToUnderlyingType(session_state));
		}
	}

	void PushApplication::StopPushInternal(const std::shared_ptr<info::Push> &push, std::shared_ptr<pub::Session> session)
	{
		auto prev_session_state = session->GetState();

		switch (prev_session_state)
		{
			case pub::Session::SessionState::Started:
				session->Stop();
				logti("Push stopped. %s", push->GetInfoString().CStr());
				break;
			case pub::Session::SessionState::Ready:
				[[fallthrough]];
			case pub::Session::SessionState::Stopping:
				[[fallthrough]];
			case pub::Session::SessionState::Stopped:
				[[fallthrough]];
			case pub::Session::SessionState::Error:
				break;
		}

		auto session_state = session->GetState();
		if (prev_session_state != session_state)
		{
			logtt("Changed push state. (%d - %d)", ov::ToUnderlyingType(prev_session_state), ov::ToUnderlyingType(session_state));
		}
	}

	void PushApplication::SessionControlThread()
	{
		ov::logger::ThreadHelper thread_helper;

		ov::StopWatch timer;
		int64_t timer_interval = 1000;
		timer.Start();

		while (!_session_control_stop_thread_flag)
		{
			if (timer.IsElapsed(timer_interval) && timer.Update())
			{
				// list of Push to be deleted
				std::vector<std::shared_ptr<info::Push>> remove_pushes;
				// Replication of pushes
				std::map<ov::String, std::shared_ptr<info::Push>> replication_pushes;

				if (true)
				{
					// Copy the push list to the replication list.
					// Avoid locking while starting/ending multiple pushes.
					std::lock_guard<std::shared_mutex> lock(_push_map_mutex);
					std::copy(_pushes.begin(), _pushes.end(), std::inserter(replication_pushes, replication_pushes.begin()));
				}

				for (auto &[id, push] : replication_pushes)
				{
					(void)(id);

					// If it is a removed push job, add to the remove waiting list
					if (push->GetRemove() == true)
					{
						remove_pushes.push_back(push);
					}

					// Find a stream by stream name
					auto stream = GetStream(push->GetStreamName());
					if (stream == nullptr || stream->GetState() != pub::Stream::State::STARTED)
					{
						logtt("There is no stream for Push or it has not started. %s", push->GetInfoString().CStr());
						push->SetState(info::Push::PushState::Ready);
						continue;
					}

					// Find a session by session ID
					auto session = stream->GetSession(push->GetSessionId());
					if (session == nullptr || push->GetSessionId() == 0)
					{
						session = stream->CreatePushSession(push);
						if (session == nullptr)
						{
							logte("Could not create session");
							continue;
						}

						push->SetSessionId(session->GetId());
					}

					// Starts only in the enabled state and stops otherwise
					if (push->GetEnable() == true && push->GetRemove() == false)
					{
						StartPushInternal(push, session);
					}
					else
					{
						StopPushInternal(push, session);
					}
				}

				if (true)
				{
					std::unique_lock<std::shared_mutex> lock(_push_map_mutex);
					for (auto &push : remove_pushes)
					{
						auto stream = GetStream(push->GetStreamName());
						if (stream != nullptr)
						{
							stream->RemoveSession(push->GetSessionId());
						}

						_pushes.erase(push->GetId());
					}
				}
			}

			usleep(100 * 1000);	 // 100ms
		}
	}

	std::vector<std::shared_ptr<info::Push>> PushApplication::GetPushesByStreamMap(const ov::String &xml_path, const std::shared_ptr<info::Stream> &stream_info)
	{
		std::vector<std::shared_ptr<info::Push>> results;

		ov::String xml_real_path = ov::GetFilePath(xml_path, cfg::ConfigManager::GetInstance()->GetConfigPath());

		pugi::xml_document xml_doc;
		auto load_result = xml_doc.load_file(xml_real_path.CStr());
		if (load_result == false)
		{
			logte("Stream(%s/%s) - Failed to load stream map file(%s) status(%d) description(%s)", GetVHostAppName().CStr(), stream_info->GetName().CStr(), xml_real_path.CStr(), load_result.status, load_result.description());
			return results;
		}

		auto root_node = xml_doc.child("PushInfo");
		if (root_node.empty())
		{
			logte("Stream(%s/%s) - Failed to load Record info file(%s) because root node is not found", GetVHostAppName().CStr(), stream_info->GetName().CStr(), xml_real_path.CStr());
			return results;
		}

		for (pugi::xml_node curr_node = root_node.child("Push"); curr_node; curr_node = curr_node.next_sibling("Push"))
		{
			bool enable = (strcmp(curr_node.child_value("Enable"), "true") == 0) ? true : false;
			if(enable == false)
			{
				continue;
			}

			// Get configuration values
			ov::String map_id = curr_node.child_value("Id");
			ov::String target_stream_name = curr_node.child_value("StreamName");
			ov::String variant_names = curr_node.child_value("VariantNames");
			ov::String protocol = curr_node.child_value("Protocol");
			ov::String url = curr_node.child_value("Url");
			ov::String stream_key = curr_node.child_value("StreamKey");

			// Get the source stream name. If no linked input stream, use the current output stream name.
			ov::String source_stream_name = stream_info->GetName();
			if (stream_info->GetLinkedInputStream() != nullptr)
			{
				source_stream_name = stream_info->GetLinkedInputStream()->GetName();
			}
			else
			{
				source_stream_name = stream_info->GetName();
			}

			// stream_name can be regex
			target_stream_name = target_stream_name.Replace("${SourceStream}", source_stream_name.CStr());

			ov::Regex _target_stream_name_regex = ov::Regex::CompiledRegex(ov::Regex::WildCardRegex(target_stream_name));
			auto match_result = _target_stream_name_regex.Matches(stream_info->GetName().CStr());

			if (!match_result.IsMatched())
			{
				continue;
			}

			// Macro replacement for stream name
			url = url.Replace("${Application}", stream_info->GetApplicationName());			
			url = url.Replace("${Stream}", stream_info->GetName().CStr());
			url = url.Replace("${SourceStream}", source_stream_name.CStr());

			// Macro replacement for stream key
			if(stream_key.IsEmpty() == false)
			{
				stream_key = stream_key.Replace("${Application}", stream_info->GetApplicationName());			
				stream_key = stream_key.Replace("${Stream}", stream_info->GetName().CStr());
				stream_key = stream_key.Replace("${SourceStream}", source_stream_name.CStr());
			}

			auto push = std::make_shared<info::Push>();
			if(push == nullptr)
			{
				continue;
			}

			// Generate unique id
			auto id = ov::Random::GenerateString(6);
			while(GetPushById(id) != nullptr)
			{
				id = ov::Random::GenerateString(6);
			}

			push->SetId(id);
			push->SetEnable(enable);
			push->SetByConfig(true);
			push->SetVhost(GetVHostAppName().GetVHostName());
			push->SetApplication(GetVHostAppName().GetAppName());
			push->SetStreamName(stream_info->GetName().CStr());
			if(variant_names.IsEmpty() == false)
			{
				auto variant_name_list = ov::String::Split(variant_names.Trim(), ",");
				for(auto variant_name : variant_name_list)
				{
					push->AddVariantName(variant_name);
				}
			}
			push->SetProtocol(protocol);
			push->SetUrl(url);
			push->SetStreamKey(stream_key);

			auto connection_timeout = GetConfig().GetPublishers().GetPushPublisher().GetConnectionTimeoutMs();
			auto send_timeout		= GetConfig().GetPublishers().GetPushPublisher().GetSendTimeoutMs();
			if (connection_timeout > 0)
			{
				push->SetConnectionTimeout(connection_timeout);
			}
			if (send_timeout > 0)
			{
				push->SetSendTimeout(send_timeout);
			}

			// logte("Push info loaded. %s", push->GetInfoString().CStr());

			// Validation check for push info
			ov::String error_message;
			if (Validate(push, error_message) == false)
			{
				logte("Failed to validate push info. %s", error_message.CStr());
				continue;
			}

			results.push_back(push);
		}
		return results;
	}

	bool PushApplication::Validate(const std::shared_ptr<info::Push> &push, ov::String &error_message)
	{
		// Validation check for required parameters
		if (push->GetId().IsEmpty() == true || push->GetStreamName().IsEmpty() == true || push->GetUrl().IsEmpty() == true || push->GetProtocol().IsEmpty() == true)
		{
			error_message = "There is no required parameter [";

			if (push->GetId().IsEmpty() == true)
			{
				error_message += " id";
			}

			if (push->GetStreamName().IsEmpty() == true)
			{
				error_message += " stream.name";
			}

			if (push->GetUrl().IsEmpty() == true)
			{
				error_message += " url";
			}

			if (push->GetProtocol().IsEmpty() == true)
			{
				error_message += " protocol";
			}

			error_message += "]";

			return false;
		}

		// Remove suffix '/" of rtmp url
		while (push->GetUrl().HasSuffix("/"))
		{
			ov::String tmp_url = push->GetUrl().Substring(0, push->GetUrl().IndexOfRev('/'));
			push->SetUrl(tmp_url);
		}

		auto url = ov::Url::Parse(push->GetUrl());
		if (url == nullptr)
		{
			logte("Could not parse url: %s", push->GetUrl().CStr());
			error_message = "Could not parse url";
			return false;
		}

		// Validation check for protocol
		if(push->GetProtocolType() == info::Push::ProtocolType::RTMP)
		{
			// Validation check for protocol scheme
			if (push->GetUrl().HasPrefix("rtmp://") == false && push->GetUrl().HasPrefix("rtmps://") == false)
			{
				error_message = "Unsupported protocol";

				return false;
			}
		}
		else if(push->GetProtocolType() == info::Push::ProtocolType::SRT)
		{
			// Validation check for protocol scheme
			if (push->GetUrl().HasPrefix("srt://") == false)
			{
				error_message = "Unsupported protocol";

				return false;
			}

			// Validation check for srt's connection mode.
			auto connection_mode = url->GetQueryValue("mode");
			if (!connection_mode.IsEmpty() &&
				connection_mode != "caller")
			{
				error_message = "Unsupported connection mode";
				return false;
			}
		}
		else if(push->GetProtocolType() == info::Push::ProtocolType::MPEGTS)
		{
			// Validation check for protocol scheme
			if (push->GetUrl().HasPrefix("udp://") == false &&
				push->GetUrl().HasPrefix("tcp://") == false)
			{
				error_message = "Unsupported protocol";
				return false;
			}

			// Validation check only push not tcp listen
			if (push->GetUrl().HasPrefix("tcp://") &&
				push->GetUrl().HasSuffix("listen"))
			{
				error_message = "Unsupported tcp listen";
				return false;
			}
		}
		else
		{
			error_message = "Unsupported protocol";

			return false;
		}

		return true;
	}	

}  // namespace pub