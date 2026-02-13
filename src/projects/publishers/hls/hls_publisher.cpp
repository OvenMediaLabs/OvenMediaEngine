//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "hls_publisher.h"

#include <base/ovlibrary/url.h>

#include "hls_private.h"
#include "hls_session.h"

std::shared_ptr<HlsPublisher> HlsPublisher::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
{
	auto ts = std::make_shared<HlsPublisher>(server_config, router);

	return ts->Start() ? ts : nullptr;
}

HlsPublisher::HlsPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	: Publisher(server_config, router)
{
	logtt("HlsPublisher has been create");
}

HlsPublisher::~HlsPublisher()
{
	logtt("HlsPublisher has been terminated finally");
}

bool HlsPublisher::PrepareHttpServers(
	const std::vector<ov::String> &ip_list,
	const bool is_port_configured, const uint16_t port,
	const bool is_tls_port_configured, const uint16_t tls_port,
	const int worker_count)
{
	auto http_server_manager = http::svr::HttpServerManager::GetInstance();

	std::vector<std::shared_ptr<http::svr::HttpServer>> http_server_list;
	std::vector<std::shared_ptr<http::svr::HttpsServer>> https_server_list;

	if (http_server_manager->CreateServers(
			GetPublisherName(), "HLSv3",
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

bool HlsPublisher::Start()
{
	auto server_config = GetServerConfig();

	auto ts_bind_config = server_config.GetBind().GetPublishers().GetHls();
	if (ts_bind_config.IsParsed() == false)
	{
		logtw("%s is disabled by configuration", GetPublisherName());
		return true;
	}

	bool is_configured = false;
	auto worker_count = ts_bind_config.GetWorkerCount(&is_configured);
	worker_count = is_configured ? worker_count : HTTP_SERVER_USE_DEFAULT_COUNT;

	bool is_port_configured;
	auto &port_config = ts_bind_config.GetPort(&is_port_configured);

	bool is_tls_port_configured;
	auto &tls_port_config = ts_bind_config.GetTlsPort(&is_tls_port_configured);

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

bool HlsPublisher::Stop()
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

bool HlsPublisher::OnCreateHost(const info::Host &host_info)
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

bool HlsPublisher::OnDeleteHost(const info::Host &host_info)
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

bool HlsPublisher::OnUpdateCertificate(const info::Host &host_info)
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

std::shared_ptr<pub::Application> HlsPublisher::OnCreatePublisherApplication(const info::Application &application_info)
{
	if (IsModuleAvailable() == false)
	{
		return nullptr;
	}

	return HlsApplication::Create(HlsPublisher::GetSharedPtrAs<pub::Publisher>(), application_info);
}

bool HlsPublisher::OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application)
{
	return true;
}

std::shared_ptr<TsHttpInterceptor> HlsPublisher::CreateInterceptor()
{
	auto http_interceptor = std::make_shared<TsHttpInterceptor>();

	// Register Request Handler
	http_interceptor->Register(http::Method::Options, R"((.+\.m3u8$)|(.+hls\.ts$)|(.+hls\.vtt$))", [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
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

		auto application = std::static_pointer_cast<HlsApplication>(GetApplicationByName(vhost_app_name));
		if (application != nullptr)
		{
			application->GetCorsManager().SetupHttpCorsHeader(vhost_app_name, request, response, {http::Method::Options, http::Method::Get, http::Method::Head});
		}

		response->SetStatusCode(http::StatusCode::OK);
		response->SetHeader("Access-Control-Allow-Private-Network", "true");

		return http::svr::NextHandler::DoNotCall;
	});

	http_interceptor->Register(http::Method::Get, R"((.+\.m3u8$)|(.+hls\.ts$)|(.+hls\.vtt$))", [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
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

		logtt("HLSv3 requested(connection : %u): %s", connection->GetId(), request->GetUri().CStr());

		auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());

		auto host_name = final_url->Host();
		auto stream_name = final_url->Stream();

		uint64_t session_life_time = 0;
		//bool access_control_enabled = IsAccessControlEnabled(final_url);
		bool access_control_enabled = false; // Not yet supported

		// Check if the request is for the master playlist
		if (access_control_enabled == true && (final_url->File().IndexOf(".m3u8") > 0 && final_url->File().IndexOf("medialist") == -1))
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

		if (vhost_app_name.IsValid() == false)
		{
			logte("Could not resolve application name from domain: %s", final_url->Host().CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			return http::svr::NextHandler::DoNotCall;
		}

		auto application = std::static_pointer_cast<HlsApplication>(GetApplicationByName(vhost_app_name));
		if (application == nullptr)
		{
			logte("Cannot find application (%s)", vhost_app_name.CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			return http::svr::NextHandler::DoNotCall;
		}

		// Cors Setting
		application->GetCorsManager().SetupHttpCorsHeader(vhost_app_name, request, response, {http::Method::Options, http::Method::Get, http::Method::Head});

		auto stream = std::static_pointer_cast<HlsStream>(GetStream(vhost_app_name, stream_name));
		if (stream == nullptr)
		{
			stream = std::dynamic_pointer_cast<HlsStream>(PullStream(final_url, vhost_app_name, host_name, stream_name));
			if (stream != nullptr)
			{
				logti("URL %s is requested", stream->GetMediaSource().CStr());
			}
			else
			{
				logte("Cannot find stream (%s/%s)", vhost_app_name.CStr(), stream_name.CStr());
				response->SetStatusCode(http::StatusCode::NotFound);
				return http::svr::NextHandler::DoNotCall;
			}
		}

		// TODO(Getroot): Improve this so that the player's first request is played immediately. This policy was temporarily changed due to a performance issue at the edge.
		if (stream->WaitUntilStart(0) == false)
		{
			logtw("(%s/%s) stream has created but not started yet", vhost_app_name.CStr(), stream_name.CStr());
			response->SetStatusCode(http::StatusCode::Created);
			return http::svr::NextHandler::DoNotCall;
		}

		// Now only Origin Mode is supported.
		auto origin_mode = application->IsOriginMode();

		std::shared_ptr<HlsSession> session = nullptr;

		// * Master Playlist
		// http[s]://<host>:<port>/<application_name>/<stream_name>/ts:playlist.m3u8 
		// http[s]://<host>:<port>/<application_name>/<stream_name>/playlist.m3u8?format=ts

		// * Media Playlist
		// http[s]://<host>:<port>/<application_name>/<stream_name>/medialist_<variant_name>_hls.m3u8

		// * Segment File
		// http[s]://<host>:<port>/<application_name>/<stream_name>/seg_<variant_name>_<number>_hls.ts

		if (origin_mode == true)
		{
			// Random session from pool
			session = stream->GetSessionFromPool();
			if (session == nullptr)
			{
				logtc("Could not get hls origin session from pool for request: %s", request->ToString().CStr());
				response->SetStatusCode(http::StatusCode::InternalServerError);
				return http::svr::NextHandler::DoNotCall;
			}
		}
		else 
		{
			// Master playlist (.m3u8 and NOT *medialist*.m3u8)
			if (final_url->File().IndexOf(".m3u8") > 0 && final_url->File().HasPrefix("medialist") == false)
			{
				session_id_t session_id = connection->GetId();

				auto session_path_ptr = std::any_cast<info::Session::Path>(connection->GetUserData(stream->GetStreamId()));
				if (session_path_ptr != nullptr)
				{
					session = std::static_pointer_cast<HlsSession>(GetSession(*session_path_ptr));
				}
				else 
				{
					session = std::static_pointer_cast<HlsSession>(stream->GetSession(session_id));
				}

				if (session == nullptr || session->GetStream() != stream)
				{
					// New HTTP Connection
					session = HlsSession::Create(session_id, origin_mode, "", stream->GetApplication(), stream, request->GetHeader("USER-AGENT"), session_life_time);
					if (session == nullptr)
					{
						logte("Could not create ts session for request: %s", request->ToString().CStr());
						response->SetStatusCode(http::StatusCode::InternalServerError);
						return http::svr::NextHandler::DoNotCall;
					}
					session->SetRequestedUrl(requested_url);
					session->SetFinalUrl(final_url);

					stream->AddSession(session);
				}
			}
			// medialist_x_hls.m3u8?session=<session id>_<key>
			// seg_x_x_hls.ts?session=<session id>_<key>
			else
			{
				session_id_t session_id = connection->GetId();
				ov::String session_key;

				// ?session=<session id>_<key>
				// This collects them into one session even if one player connects through multiple connections.
				auto query_string = final_url->GetQueryValue("session");
				auto id_key = query_string.Split("_");
				if (id_key.size() != 2)
				{
					logte("Invalid session key : %s", final_url->ToUrlString().CStr());
					response->SetStatusCode(http::StatusCode::Unauthorized);
					return http::svr::NextHandler::DoNotCall;
				}

				session_id = ov::Converter::ToUInt32(id_key[0].CStr());
				session_key = id_key[1];

				session = std::static_pointer_cast<HlsSession>(stream->GetSession(session_id));
				if (session == nullptr)
				{
					if (access_control_enabled == true)
					{
						logte("Invalid session_key : %s", final_url->ToUrlString().CStr());
						response->SetStatusCode(http::StatusCode::Unauthorized);
						return http::svr::NextHandler::DoNotCall;
					}
					else
					{
						// New HTTP Connection
						session = HlsSession::Create(session_id, origin_mode, session_key, stream->GetApplication(), stream, session_life_time);
						if (session == nullptr)
						{
							logte("Could not create ts session for request: %s", request->ToString().CStr());
							response->SetStatusCode(http::StatusCode::InternalServerError);
							return http::svr::NextHandler::DoNotCall;
						}
						session->SetRequestedUrl(requested_url);
						session->SetFinalUrl(final_url);
						stream->AddSession(session);
					}
				}
				else
				{
					if (access_control_enabled == true && session_key != session->GetSessionKey())
					{
						logte("Invalid session_key : %s", final_url->ToUrlString().CStr());
						response->SetStatusCode(http::StatusCode::Unauthorized);
						return http::svr::NextHandler::DoNotCall;
					}
				}
			}

			// It will be used in CloseHandler
			connection->AddUserData(stream->GetStreamId(), std::move(session->GetSessionPath()));
			session->UpdateLastRequest(connection->GetId());
		}

		stream->SendMessage(session, std::make_any<std::shared_ptr<http::svr::HttpExchange>>(exchange));

		return http::svr::NextHandler::DoNotCallAndDoNotResponse;
	});

	// Set Close Handler
	http_interceptor->SetCloseHandler([this](const std::shared_ptr<http::svr::HttpConnection> &connection, PhysicalPortDisconnectReason reason) -> void {
		for (auto &user_data : connection->GetUserDataMap())
		{
			std::shared_ptr<HlsSession> session;
			
			auto session_path_ptr = std::any_cast<info::Session::Path>(&user_data.second);
			if (session_path_ptr == nullptr)
			{
				continue;
			}

			session = std::static_pointer_cast<HlsSession>(GetSession(*session_path_ptr));
			if (session != nullptr)
			{
				session->OnConnectionDisconnected(connection->GetId());
				if (session->IsNoConnection())
				{
					logtt("ts session is closed : %u", session->GetId());
					// Remove session from stream
					auto stream = session->GetStream();
					if (stream != nullptr)
					{
						auto remote_address = connection->GetSocket()->GetRemoteAddress();
						auto requested_url = session->GetRequestedUrl();
						auto final_url = session->GetFinalUrl();
						if (remote_address && requested_url && final_url)
						{
							auto request_info = std::make_shared<ac::RequestInfo>(requested_url, final_url, connection->GetSocket(), nullptr);
							request_info->SetUserAgent(session->GetUserAgent());

							SendCloseAdmissionWebhooks(request_info);
						}

						stream->RemoveSession(session->GetId());
					}
				}
			}
		}
	});

	return http_interceptor;
}