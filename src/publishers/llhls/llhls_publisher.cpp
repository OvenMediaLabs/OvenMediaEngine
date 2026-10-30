//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "llhls_publisher.h"

#include <base/ovlibrary/url.h>
#include <modules/task_pool/task_pool.h>

#include "llhls_private.h"
#include "llhls_session.h"

// Connection user data keys of LLHLS sessions, one for each session the connection carried
static constexpr const char *kSessionUserDataPrefix = "llhls-session/";

static ov::String MakeSessionUserDataKey(const info::Session::Path &path)
{
	return ov::String::FormatString("%s%u/%u/%u", kSessionUserDataPrefix, path._application_id, path._stream_id, path._session_id);
}

std::shared_ptr<LLHlsPublisher> LLHlsPublisher::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
{
	auto llhls = std::make_shared<LLHlsPublisher>(server_config, router);

	return llhls->Start() ? llhls : nullptr;
}

LLHlsPublisher::LLHlsPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	: Publisher(server_config, router)
{
	logtt("LLHlsPublisher has been create");
}

LLHlsPublisher::~LLHlsPublisher()
{
	logtt("LLHlsPublisher has been terminated finally");
}

bool LLHlsPublisher::PrepareHttpServers(
	const std::vector<ov::String> &ip_list,
	const bool is_port_configured, const uint16_t port,
	const bool is_tls_port_configured, const uint16_t tls_port,
	const int worker_count)
{
	auto http_server_manager = http::svr::HttpServerManager::GetInstance();

	std::vector<std::shared_ptr<http::svr::HttpServer>> http_server_list;
	std::vector<std::shared_ptr<http::svr::HttpsServer>> https_server_list;

	if (http_server_manager->CreateServers(
			GetPublisherName(), "LLHLS",
			&http_server_list, &https_server_list,
			ip_list,
			is_port_configured, port,
			is_tls_port_configured, tls_port,
			nullptr, false,
			[&](const ov::SocketAddress &address, bool is_https, const std::shared_ptr<http::svr::HttpServer> &http_server) {
				http_server->AddInterceptor(CreateInterceptor());
			},
			worker_count))
	{
		std::lock_guard lock_guard{_http_server_list_mutex};
		_http_server_list = std::move(http_server_list);
		_https_server_list = std::move(https_server_list);

		return true;
	}

	http_server_manager->ReleaseServers(&http_server_list);
	http_server_manager->ReleaseServers(&https_server_list);

	return false;
}

bool LLHlsPublisher::Start()
{
	auto server_config = GetServerConfig();

	auto llhls_bind_config = server_config.GetBind().GetPublishers().GetLLHls();
	if (llhls_bind_config.IsParsed() == false)
	{
		logtw("%s is disabled by configuration", GetPublisherName());
		return true;
	}

	auto llhls_module_config = server_config.GetModules().GetLLHls();
	if (llhls_module_config.IsEnabled() == false)
	{
		logtw("%s is disabled by configuration (Module is disabled)", GetPublisherName());
		return true;
	}

	bool is_configured = false;
	auto worker_count = llhls_bind_config.GetWorkerCount(&is_configured);
	worker_count = is_configured ? worker_count : HTTP_SERVER_USE_DEFAULT_COUNT;

	bool is_port_configured;
	auto &port_config = llhls_bind_config.GetPort(&is_port_configured);

	bool is_tls_port_configured;
	auto &tls_port_config = llhls_bind_config.GetTlsPort(&is_tls_port_configured);

	if ((is_port_configured == false) && (is_tls_port_configured == false))
	{
		logtw("%s Server is disabled - No port is configured", GetPublisherName());
		return true;
	}

	return PrepareHttpServers(
			   server_config.GetIPList(),
			   is_port_configured, port_config.GetPort(),
			   is_tls_port_configured, tls_port_config.GetPort(),
			   worker_count) &&
		   Publisher::Start();
}

bool LLHlsPublisher::Stop()
{
	_http_server_list_mutex.lock();
	auto http_server_list = std::move(_http_server_list);
	auto https_server_list = std::move(_https_server_list);
	_http_server_list_mutex.unlock();

	auto http_server_manager = http::svr::HttpServerManager::GetInstance();

	http_server_manager->ReleaseServers(&http_server_list);
	http_server_manager->ReleaseServers(&https_server_list);

	return Publisher::Stop();
}

bool LLHlsPublisher::OnCreateHost(const info::Host &host_info)
{
	bool result = true;
	auto certificate = host_info.GetCertificate();

	if (certificate != nullptr)
	{
		std::lock_guard lock_guard{_http_server_list_mutex};

		for (auto &https_server : _https_server_list)
		{
			if (https_server->InsertCertificate(certificate) != nullptr)
			{
				result = false;
			}
		}
	}

	return result;
}

bool LLHlsPublisher::OnDeleteHost(const info::Host &host_info)
{
	bool result = true;
	auto certificate = host_info.GetCertificate();

	if (certificate != nullptr)
	{
		std::lock_guard lock_guard{_http_server_list_mutex};

		for (auto &https_server : _https_server_list)
		{
			if (https_server->RemoveCertificate(certificate) != nullptr)
			{
				result = false;
			}
		}
	}

	return result;
}

bool LLHlsPublisher::OnUpdateCertificate(const info::Host &host_info)
{
	bool result = true;
	auto certificate = host_info.GetCertificate();

	if (certificate != nullptr)
	{
		std::lock_guard lock_guard{_http_server_list_mutex};

		for (auto &https_server : _https_server_list)
		{
			if (https_server->InsertCertificate(certificate) != nullptr)
			{
				result = false;
			}
		}
	}

	return result;
}

std::shared_ptr<pub::Application> LLHlsPublisher::OnCreatePublisherApplication(const info::Application &application_info)
{
	if (IsModuleAvailable() == false)
	{
		return nullptr;
	}

	return LLHlsApplication::Create(LLHlsPublisher::GetSharedPtrAs<pub::Publisher>(), application_info);
}

bool LLHlsPublisher::OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application)
{
	return true;
}

std::shared_ptr<LLHlsHttpInterceptor> LLHlsPublisher::CreateInterceptor()
{
	auto http_interceptor = std::make_shared<LLHlsHttpInterceptor>();

	// Register Request Handler
	http_interceptor->Register(http::Method::Options, R"((.+\.m3u8$)|(.+llhls\.(m4s|vtt)$))", [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
		auto connection = exchange->GetConnection();
		auto request = exchange->GetRequest();
		auto response = exchange->GetResponse();

		auto request_url = request->GetParsedUri();
		if (request_url == nullptr)
		{
			logte("Could not parse request url: %s", request->GetUri().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto vhost_name = ocst::Orchestrator::GetInstance()->GetVhostNameFromDomain(request_url->Host());
		if (vhost_name.IsEmpty())
		{
			logte("Could not resolve vhost name from domain: %s", request_url->Host().CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			return http::svr::NextHandler::DoNotCall;
		}

		auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(request_url->Host(), request_url->App());
		if (vhost_app_name.IsValid() == false)
		{
			logte("Could not resolve application name from domain: %s", request_url->Host().CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			return http::svr::NextHandler::DoNotCall;
		}

		auto application = std::static_pointer_cast<LLHlsApplication>(GetApplicationByName(vhost_app_name));
		if (application != nullptr)
		{
			application->GetCorsManager().SetupHttpCorsHeader(vhost_app_name, request, response, {http::Method::Options, http::Method::Get, http::Method::Head});
		}

		response->SetStatusCode(http::StatusCode::OK);
		response->SetHeader("Access-Control-Allow-Private-Network", "true");

		return http::svr::NextHandler::DoNotCall;
	});

	http_interceptor->Register(http::Method::Get, R"((.+\.m3u8$)|(.+llhls\.(m4s|vtt)$))", [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
		auto connection = exchange->GetConnection();
		auto request = exchange->GetRequest();
		auto response = exchange->GetResponse();
		auto remote_address = request->GetRemote()->GetRemoteAddress();

		auto final_url = request->GetParsedUri();
		if (final_url == nullptr)
		{
			logte("Could not parse request url: %s", request->GetUri().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		// PORT can be omitted if port is default port, but SignedPolicy requires this information.
		if (final_url->Port() == 0)
		{
			final_url->SetPort(request->GetRemote()->GetLocalAddress()->Port());
		}
		auto requested_url = final_url;

		logtt("LLHLS requested(connection : %u, fd : %d): %s", connection->GetId(), connection->GetSocket()->GetNativeHandle(), request->GetUri().CStr());

		auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());

		auto host_name = final_url->Host();
		auto stream_name = final_url->Stream();

		uint64_t session_life_time = 0;
		bool access_control_enabled = IsAccessControlEnabled(final_url);
		bool admitted_by_webhooks = false;

		// Check if the request is for the master playlist
		if (access_control_enabled == true && (final_url->File().IndexOf(".m3u8") > 0 && final_url->File().IndexOf("chunklist") == -1))
		{
			auto request_info = std::make_shared<ac::RequestInfo>(final_url, nullptr, request->GetRemote(), request);

			auto [signed_policy_result, signed_policy] = Publisher::VerifyBySignedPolicy(request_info);
			if (signed_policy_result == AccessController::VerificationResult::Pass)
			{
				session_life_time = signed_policy->GetStreamExpireEpochMSec();
			}
			else if (signed_policy_result == AccessController::VerificationResult::Error)
			{
				logte("Could not resolve application name from domain: %s", final_url->Host().CStr());
				response->SetStatusCode(http::StatusCode::Unauthorized);
				return http::svr::NextHandler::DoNotCall;
			}
			else if (signed_policy_result == AccessController::VerificationResult::Fail)
			{
				logtw("%s", signed_policy->GetErrMessage().CStr());
				response->SetStatusCode(http::StatusCode::Unauthorized);
				return http::svr::NextHandler::DoNotCall;
			}

			// Admission Webhooks
			auto [webhooks_result, admission_webhooks] = VerifyByAdmissionWebhooks(request_info);
			if (webhooks_result == AccessController::VerificationResult::Off)
			{
				// Success
			}
			else if (webhooks_result == AccessController::VerificationResult::Pass)
			{
				admitted_by_webhooks = true;

				// Lifetime
				if (admission_webhooks->GetLifetime() != 0)
				{
					// Choice smaller value
					auto stream_expired_msec_from_webhooks = ov::Clock::NowMSec() + admission_webhooks->GetLifetime();
					if (session_life_time == 0 || stream_expired_msec_from_webhooks < session_life_time)
					{
						session_life_time = stream_expired_msec_from_webhooks;
					}
				}

				// Redirect URL
				if (admission_webhooks->GetNewURL() != nullptr)
				{
					final_url = admission_webhooks->GetNewURL();
					if (final_url->Port() == 0)
					{
						final_url->SetPort(request->GetRemote()->GetLocalAddress()->Port());
					}

					vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());
					host_name = final_url->Host();
					stream_name = final_url->Stream();
				}
			}
			else if (webhooks_result == AccessController::VerificationResult::Error)
			{
				logtw("AdmissionWebhooks error : %s", final_url->ToUrlString().CStr());
				response->SetStatusCode(http::StatusCode::Unauthorized);
				return http::svr::NextHandler::DoNotCall;
			}
			else if (webhooks_result == AccessController::VerificationResult::Fail)
			{
				logtw("AdmissionWebhooks error : %s", admission_webhooks->GetErrReason().CStr());
				response->SetStatusCode(http::StatusCode::Unauthorized);
				return http::svr::NextHandler::DoNotCall;
			}
		}

		// A master playlist request the control server allowed is closed as well when it ends without a session
		auto close_admission = [&]() {
			if (admitted_by_webhooks == true)
			{
				PostCloseAdmissionWebhooks(std::make_shared<ac::RequestInfo>(requested_url, final_url, request->GetRemote(), request));
			}
		};

		if (vhost_app_name.IsValid() == false)
		{
			logte("Could not resolve application name from domain: %s", final_url->Host().CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			close_admission();
			return http::svr::NextHandler::DoNotCall;
		}

		auto application = std::static_pointer_cast<LLHlsApplication>(GetApplicationByName(vhost_app_name));
		auto stream = std::static_pointer_cast<LLHlsStream>(GetStream(vhost_app_name, stream_name));
		if (stream == nullptr)
		{
			// PullStream may create the app from the wildcard template
			stream = std::dynamic_pointer_cast<LLHlsStream>(PullStream(final_url, vhost_app_name, host_name, stream_name));
			if (stream != nullptr)
			{
				if (application == nullptr)
				{
					application = std::static_pointer_cast<LLHlsApplication>(GetApplicationByName(vhost_app_name));
				}
				logti("URL %s is requested", stream->GetMediaSource().CStr());
			}
		}

		// Apply CORS once the application is known so error responses still carry the headers
		if (application != nullptr)
		{
			application->GetCorsManager().SetupHttpCorsHeader(vhost_app_name, request, response, {http::Method::Options, http::Method::Get, http::Method::Head});
		}

		if (application == nullptr || stream == nullptr)
		{
			logte("Cannot find stream (%s/%s)", vhost_app_name.CStr(), stream_name.CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			close_admission();
			return http::svr::NextHandler::DoNotCall;
		}

		// TODO(Getroot): Improve this so that the player's first request is played immediately. This policy was temporarily changed due to a performance issue at the edge.
		if (stream->WaitUntilStart(0) == false)
		{
			logtw("(%s/%s) stream has created but not started yet", vhost_app_name.CStr(), stream_name.CStr());
			response->SetStatusCode(http::StatusCode::Created);
			close_admission();
			return http::svr::NextHandler::DoNotCall;
		}

		auto origin_mode = application->IsOriginMode();

		std::shared_ptr<LLHlsSession> session = nullptr;

		// Master playlist (.m3u8 and NOT *chunklist*.m3u8)
		if (origin_mode == true)
		{
			// Random session from pool
			session = stream->GetSessionFromPool();
			if (session == nullptr)
			{
				logtc("Could not get llhls origin session from pool for request: %s", request->ToString().CStr());
				response->SetStatusCode(http::StatusCode::InternalServerError);
				close_admission();
				return http::svr::NextHandler::DoNotCall;
			}
		}
		else
		{
			if (final_url->File().HasSuffix(".m3u8") == true && final_url->File().HasPrefix("chunklist") == false)
			{
				// One player is one session, and a master playlist request comes without a session value, so it always starts one
				session = LLHlsSession::Create(stream->IssueUniqueSessionId(), origin_mode, "", stream->GetApplication(), stream, request->GetHeader("USER-AGENT"), session_life_time);
				if (session == nullptr)
				{
					logte("Could not create llhls session for request: %s", request->ToString().CStr());
					response->SetStatusCode(http::StatusCode::InternalServerError);
					close_admission();
					return http::svr::NextHandler::DoNotCall;
				}
				session->SetRequestedUrl(requested_url);
				session->SetFinalUrl(final_url);
				session->UpdateLastRequest(connection->GetId());

				stream->RegisterSession(session);
			}
			// chunklist_x_x.m3u8?session=<session id>_<key>
			// x_x_x.m4s?session=<session id>_<key>
			else
			{
				// ?session=<session id>_<key>
				// This collects them into one session even if one player connects through multiple connections.
				auto session_value = final_url->GetQueryValue("session");
				// The value is written back into playlists, so only the form OME issues is taken
				if (LLHlsSession::IsSessionValue(session_value) == false)
				{
					logte("Invalid session key : %s", final_url->ToUrlString().CStr());
					response->SetStatusCode(http::StatusCode::Unauthorized);
					return http::svr::NextHandler::DoNotCall;
				}

				session = stream->GetSessionByValue(session_value);
				if (access_control_enabled == true)
				{
					// A session whose connections all left closes after a short grace, then the player must come back through the master playlist
					if (session == nullptr || session->UpdateLastRequest(connection->GetId()) == false)
					{
						// The number of a live session with another key means the key was made up
						if (session == nullptr && stream->GetSession(ov::Converter::ToUInt32(session_value.CStr())) != nullptr)
						{
							logte("Invalid session_key : %s", final_url->ToUrlString().CStr());
						}
						else
						{
							logtw("LLHLS session has ended, so the request is rejected : %s", final_url->ToUrlString().CStr());
						}

						response->SetStatusCode(http::StatusCode::Unauthorized);
						return http::svr::NextHandler::DoNotCall;
					}
				}
				else if (session == nullptr || session->UpdateLastRequest(connection->GetId()) == false)
				{
					// Without access control any URL may start a session, in step with a closing that takes one out of the stream
					std::lock_guard<std::mutex> lock(_session_lifecycle_mutex);

					session = stream->GetSessionByValue(session_value);
					if (session == nullptr)
					{
						// New HTTP Connection, numbered like any other session and found by the value it came with
						session = LLHlsSession::Create(stream->IssueUniqueSessionId(), origin_mode, session_value, stream->GetApplication(), stream, session_life_time);
						if (session == nullptr)
						{
							logte("Could not create llhls session for request: %s", request->ToString().CStr());
							response->SetStatusCode(http::StatusCode::InternalServerError);
							return http::svr::NextHandler::DoNotCall;
						}
						session->SetRequestedUrl(requested_url);
						session->SetFinalUrl(final_url);

						stream->RegisterSession(session);
					}

					session->UpdateLastRequest(connection->GetId());
				}
			}

			// Every session that used this connection is recorded, so the close handler reaches all of them
			connection->AddUserData(MakeSessionUserDataKey(session->GetSessionPath()), session->GetSessionPath());
		}

		stream->SendMessage(session, std::make_any<std::shared_ptr<http::svr::HttpExchange>>(exchange));

		return http::svr::NextHandler::DoNotCallAndDoNotResponse;
	});

	// Set Close Handler
	http_interceptor->SetCloseHandler([this](const std::shared_ptr<http::svr::HttpConnection> &connection, PhysicalPortDisconnectReason reason) -> void {
		for (auto &user_data : connection->GetUserDataMap())
		{
			std::shared_ptr<LLHlsSession> session;

			if (user_data.first.HasPrefix(kSessionUserDataPrefix) == false)
			{
				continue;
			}

			auto session_path_ptr = std::any_cast<info::Session::Path>(&user_data.second);
			if (session_path_ptr == nullptr)
			{
				continue;
			}
			
			session = std::static_pointer_cast<LLHlsSession>(GetSession(*session_path_ptr));
			if (session != nullptr)
			{
				auto idle_since = session->OnConnectionDisconnected(connection->GetId());
				if (idle_since.has_value())
				{
					CloseSessionAfterGrace(session, idle_since.value(), connection->GetSocket());
				}
			}
		}
	});

	// Runs on each connection's own timer, so a player that stops using a shared connection is released even while other traffic keeps it open
	http_interceptor->SetRepeatHandler([this](const std::shared_ptr<http::svr::HttpConnection> &connection) -> void {
		ReleaseSessionsIdleOn(connection);
	});

	return http_interceptor;
}

void LLHlsPublisher::PostCloseAdmissionWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info)
{
	// The worker may outlive this publisher
	std::weak_ptr<LLHlsPublisher> weak_publisher = GetSharedPtrAs<LLHlsPublisher>();

	auto posted = ov::TaskPool::GetInstance()->Post([weak_publisher, request_info]() {
		auto publisher = weak_publisher.lock();
		if (publisher != nullptr)
		{
			publisher->SendCloseAdmissionWebhooks(request_info);
		}
	}, ov::TaskPool::Priority::Low, ov::TaskPool::kNoLimit);

	// Refused only while the pool stops or has no worker, and the closing must still be sent
	if (posted == false)
	{
		SendCloseAdmissionWebhooks(request_info);
	}
}

void LLHlsPublisher::ReleaseSessionsIdleOn(const std::shared_ptr<http::svr::HttpConnection> &connection)
{
	std::vector<std::pair<ov::String, std::shared_ptr<LLHlsSession>>> idle_sessions;

	for (const auto &[key, user_data] : connection->GetUserDataMap())
	{
		if (key.HasPrefix(kSessionUserDataPrefix) == false)
		{
			continue;
		}

		auto session_path = std::any_cast<info::Session::Path>(&user_data);
		if (session_path == nullptr)
		{
			continue;
		}

		// A player that stops using a shared connection is treated like one whose own connection went idle
		auto session = std::static_pointer_cast<LLHlsSession>(GetSession(*session_path));
		if (session == nullptr || session->HasUsedConnectionWithin(connection->GetId(), HTTP_CONNECTION_TIMEOUT_MS) == false)
		{
			idle_sessions.emplace_back(key, session);
		}
	}

	for (const auto &[key, session] : idle_sessions)
	{
		connection->RemoveUserData(key);

		if (session == nullptr)
		{
			continue;
		}

		auto idle_since = session->OnConnectionDisconnected(connection->GetId());
		if (idle_since.has_value())
		{
			CloseSessionAfterGrace(session, idle_since.value(), connection->GetSocket());
		}
	}
}

void LLHlsPublisher::CloseSessionAfterGrace(const std::shared_ptr<LLHlsSession> &session, uint64_t idle_since, const std::shared_ptr<const ov::Socket> &last_socket)
{
	// The worker may outlive this publisher and the session
	std::weak_ptr<LLHlsPublisher> weak_publisher = GetSharedPtrAs<LLHlsPublisher>();
	std::weak_ptr<LLHlsSession> weak_session = session;

	auto posted = ov::TaskPool::GetInstance()->PostDelayed(kSessionCloseGraceMs, [weak_publisher, weak_session, idle_since, last_socket]() {
		auto publisher = weak_publisher.lock();
		auto session = weak_session.lock();
		if (publisher == nullptr || session == nullptr)
		{
			return;
		}

		publisher->CloseSessionIfIdleSince(session, idle_since, last_socket);
	}, ov::TaskPool::Priority::High, ov::TaskPool::kNoLimit);

	// Refused only while the pool stops or has no worker, and the session must still close
	if (posted == false)
	{
		CloseSessionIfIdleSince(session, idle_since, last_socket);
	}
}

void LLHlsPublisher::CloseSessionIfIdleSince(const std::shared_ptr<LLHlsSession> &session, uint64_t idle_since, const std::shared_ptr<const ov::Socket> &last_socket)
{
	auto stream = std::static_pointer_cast<LLHlsStream>(session->GetStream());
	if (stream == nullptr)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lock(_session_lifecycle_mutex);
		if (session->CloseIfIdleSince(idle_since) == false)
		{
			return;
		}

		stream->UnregisterSession(session);
	}

	logtt("llhls session is closed : %u", session->GetId());

	auto request_info = std::make_shared<ac::RequestInfo>(session->GetRequestedUrl(), session->GetFinalUrl(), last_socket, nullptr);
	request_info->SetUserAgent(session->GetUserAgent());
	// Sent behind the other expiries, so a slow control server cannot stretch their grace
	PostCloseAdmissionWebhooks(request_info);
}
