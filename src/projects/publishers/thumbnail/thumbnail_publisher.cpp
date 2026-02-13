#include "thumbnail_publisher.h"

#include <base/ovlibrary/url.h>

#include "thumbnail_private.h"

#define UNUSED(expr)  \
	do                \
	{                 \
		(void)(expr); \
	} while (0)

std::shared_ptr<ThumbnailPublisher> ThumbnailPublisher::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
{
	auto file = std::make_shared<ThumbnailPublisher>(server_config, router);

	if (!file->Start())
	{
		return nullptr;
	}

	return file;
}

ThumbnailPublisher::ThumbnailPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	: Publisher(server_config, router)
{
	logtt("ThumbnailPublisher has been create");
}

ThumbnailPublisher::~ThumbnailPublisher()
{
	logtt("ThumbnailPublisher has been terminated finally");
}

bool ThumbnailPublisher::PrepareHttpServers(
	const std::vector<ov::String> &ip_list,
	const bool is_port_configured, const uint16_t port,
	const bool is_tls_port_configured, const uint16_t tls_port,
	const int worker_count)
{
	auto http_server_manager = http::svr::HttpServerManager::GetInstance();

	std::vector<std::shared_ptr<http::svr::HttpServer>> http_server_list;
	std::vector<std::shared_ptr<http::svr::HttpsServer>> https_server_list;

	if (http_server_manager->CreateServers(
			GetPublisherName(), "Thumb",
			&http_server_list, &https_server_list,
			ip_list,
			is_port_configured, port,
			is_tls_port_configured, tls_port,
			nullptr,
			false,
			[&](const ov::SocketAddress &address, bool is_https, const std::shared_ptr<http::svr::HttpServer> &http_server) {
				http_server->AddInterceptor(CreateInterceptor());
			},
			worker_count))
	{
		_http_server_list = std::move(http_server_list);
		_https_server_list = std::move(https_server_list);

		return true;
	}

	http_server_manager->ReleaseServers(&http_server_list);
	http_server_manager->ReleaseServers(&https_server_list);

	return false;
}

bool ThumbnailPublisher::Start()
{
	auto server_config = GetServerConfig();

	const auto &thumbnail_bind_config = server_config.GetBind().GetPublishers().GetThumbnail();
	if (thumbnail_bind_config.IsParsed() == false)
	{
		logtw("%s is disabled by configuration", GetPublisherName());
		return true;
	}

	bool is_configured = false;
	auto worker_count = thumbnail_bind_config.GetWorkerCount(&is_configured);
	worker_count = is_configured ? worker_count : HTTP_SERVER_USE_DEFAULT_COUNT;

	bool is_port_configured;
	auto &port_config = thumbnail_bind_config.GetPort(&is_port_configured);

	bool is_tls_port_configured;
	auto &tls_port_config = thumbnail_bind_config.GetTlsPort(&is_tls_port_configured);

	if ((is_port_configured == false) && (is_tls_port_configured == false))
	{
		logtw("%s is disabled - No port is configured", GetPublisherName());
		return true;
	}

	return PrepareHttpServers(
			   server_config.GetIPList(),
			   is_port_configured, port_config.GetPort(),
			   is_tls_port_configured, tls_port_config.GetPort(),
			   worker_count) &&
		   Publisher::Start();
}

bool ThumbnailPublisher::Stop()
{
	auto manager = http::svr::HttpServerManager::GetInstance();

	_http_server_list_mutex.lock();
	auto http_server_list = std::move(_http_server_list);
	auto https_server_list = std::move(_https_server_list);
	_http_server_list_mutex.unlock();

	manager->ReleaseServers(&http_server_list);
	manager->ReleaseServers(&https_server_list);

	return Publisher::Stop();
}

bool ThumbnailPublisher::OnCreateHost(const info::Host &host_info)
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

bool ThumbnailPublisher::OnDeleteHost(const info::Host &host_info)
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

bool ThumbnailPublisher::OnUpdateCertificate(const info::Host &host_info)
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

std::shared_ptr<pub::Application> ThumbnailPublisher::OnCreatePublisherApplication(const info::Application &application_info)
{
	if (IsModuleAvailable() == false)
	{
		return nullptr;
	}

	return ThumbnailApplication::Create(ThumbnailPublisher::GetSharedPtrAs<pub::Publisher>(), application_info);
}

bool ThumbnailPublisher::OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application)
{
	return true;
}

std::shared_ptr<ThumbnailInterceptor> ThumbnailPublisher::CreateInterceptor()
{
	ov::String thumbnail_url_pattern = R"(.+thumb\.(jpg|png|webp)$)";

	auto http_interceptor = std::make_shared<ThumbnailInterceptor>();

	// Register Thumbnail interceptor
	http_interceptor->Register(http::Method::Get, thumbnail_url_pattern, [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
		auto request = exchange->GetRequest();
		auto response = exchange->GetResponse();
		auto remote_address = request->GetRemote()->GetRemoteAddress();

		auto request_url = request->GetParsedUri();
		if (request_url == nullptr)
		{
			logtw("Could not parse request url: %s", request->GetRequestTarget().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		info::VHostAppName vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(request_url->Host(), request_url->App());
		ov::String host_name = request_url->Host();
		ov::String stream_name = request_url->Stream();

		// Don't validate vhost_app_name here, it can be changed by AdmissionWebhooks
		// And the application may not exist here, it can be created by PullStream 

		bool access_control_enabled = IsAccessControlEnabled(request_url);
		// Access Control
		if (access_control_enabled == true)
		{
			auto request_info = std::make_shared<ac::RequestInfo>(request_url, nullptr, request->GetRemote(), request);
			// Signed Policy
			auto [signed_policy_result, signed_policy] = Publisher::VerifyBySignedPolicy(request_info);
			if (signed_policy_result == AccessController::VerificationResult::Pass)
			{

			}
			else if (signed_policy_result == AccessController::VerificationResult::Error)
			{
				logte("Could not resolve application name from domain: %s", request_url->Host().CStr());
				response->AppendString(ov::String::FormatString("Could not resolve application name from domain"));
				response->SetStatusCode(http::StatusCode::Unauthorized);
				return http::svr::NextHandler::DoNotCall;
			}
			else if (signed_policy_result == AccessController::VerificationResult::Fail)
			{
				logtw("%s", signed_policy->GetErrMessage().CStr());
				response->AppendString(ov::String::FormatString("%s", signed_policy->GetErrMessage().CStr()));
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
				// Redirect URL
				if (admission_webhooks->GetNewURL() != nullptr)
				{
					request_url = admission_webhooks->GetNewURL();
					if (request_url->Port() == 0)
					{
						request_url->SetPort(request->GetRemote()->GetLocalAddress()->Port());
					}

					vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(request_url->Host(), request_url->App());
					host_name = request_url->Host();
					stream_name = request_url->Stream();
				}
			}
			else if (webhooks_result == AccessController::VerificationResult::Error)
			{
				logtw("AdmissionWebhooks error : %s", request_url->ToUrlString().CStr());
				response->AppendString(ov::String::FormatString("AdmissionWebhooks error"));
				response->SetStatusCode(http::StatusCode::Unauthorized);
				return http::svr::NextHandler::DoNotCall;
			}
			else if (webhooks_result == AccessController::VerificationResult::Fail)
			{
				logtw("AdmissionWebhooks error : %s", admission_webhooks->GetErrReason().CStr());
				response->AppendString(ov::String::FormatString("Unauthorized"));
				response->SetStatusCode(http::StatusCode::Unauthorized);
				return http::svr::NextHandler::DoNotCall;
			}
		}

		// Check CORS
		auto application = std::static_pointer_cast<ThumbnailApplication>(GetApplicationByName(vhost_app_name));
		if (application == nullptr)
		{
			logte("Cannot find application (%s)", vhost_app_name.CStr());
			response->AppendString("Could not found application of thumbnail publisher");
			response->SetStatusCode(http::StatusCode::NotFound);
			return http::svr::NextHandler::DoNotCall;
		}

		application->GetCorsManager().SetupHttpCorsHeader(vhost_app_name, request, response);

		// Check Stream
		auto stream = GetStream(vhost_app_name, request_url->Stream());		
		if (stream == nullptr)
		{
			// If the stream does not exists, request to the provider
			stream = PullStream(request_url, vhost_app_name, host_name, stream_name);
			if (stream == nullptr)
			{
				logte("There is no stream or cannot pull a stream. stream(%s)", request_url->Stream().CStr());
				response->AppendString("There is no stream or cannot pull a stream");
				response->SetStatusCode(http::StatusCode::NotFound);			
				return http::svr::NextHandler::DoNotCall;
			}			
		}

		if(stream->GetState() != pub::Stream::State::STARTED)
		{
			logte("The stream has not started. stream(%s)", request_url->Stream().CStr());
			response->AppendString("The stream has not started");
			response->SetStatusCode(http::StatusCode::NotFound);			
			return http::svr::NextHandler::DoNotCall;
		}

		// Check Extentions
		auto media_codec_id = cmn::MediaCodecId::None;
		if (request_url->File().LowerCaseString().IndexOf(".jpg") >= 0)
		{
			media_codec_id = cmn::MediaCodecId::Jpeg;
		}
		else if (request_url->File().LowerCaseString().IndexOf(".png") >= 0)
		{
			media_codec_id = cmn::MediaCodecId::Png;
		}
		else if (request_url->File().LowerCaseString().IndexOf(".webp") >= 0)
		{
			media_codec_id = cmn::MediaCodecId::Webp;
		}		
		else 
		{
			response->AppendString(ov::String::FormatString("Unsupported file extension"));
			response->SetStatusCode(http::StatusCode::NotFound);
			return http::svr::NextHandler::DoNotCall;	
		}

		// Wait O seconds for thumbnail image to be received
		auto endcoded_video_frame = std::static_pointer_cast<ThumbnailStream>(stream)->GetVideoFrameByCodecId(media_codec_id, 5000);
		if (endcoded_video_frame == nullptr)
		{
			response->AppendString(ov::String::FormatString("There is no thumbnail image"));
			response->SetStatusCode(http::StatusCode::NotFound);					
			return http::svr::NextHandler::DoNotCall;
		}

		response->SetHeader("Content-Type", MimeTypeFromMediaCodecId(media_codec_id));
		response->SetStatusCode(http::StatusCode::OK);
		response->AppendData(std::move(endcoded_video_frame->Clone()));
		auto sent_size = response->Response();
		exchange->Release();

		if (sent_size > 0)
		{
			MonitorInstance->IncreaseBytesOut(*stream, PublisherType::Thumbnail, sent_size);
		}
		
		return http::svr::NextHandler::DoNotCall;
	});

	// Register Preflight interceptor
	http_interceptor->Register(http::Method::Options, thumbnail_url_pattern, [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
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

		auto application = std::static_pointer_cast<ThumbnailApplication>(GetApplicationByName(vhost_app_name));
		if (application != nullptr)
		{
			application->GetCorsManager().SetupHttpCorsHeader(vhost_app_name, request, response, {http::Method::Options, http::Method::Get});
		}

		response->SetStatusCode(http::StatusCode::OK);
		response->SetHeader("Access-Control-Allow-Private-Network", "true");

		return http::svr::NextHandler::DoNotCall;
	});

	return http_interceptor;
}

ov::String ThumbnailPublisher::MimeTypeFromMediaCodecId(const cmn::MediaCodecId &type)
{
	switch (type)
	{
		case cmn::MediaCodecId::Jpeg:
			return "image/jpeg";
		case cmn::MediaCodecId::Png:
			return "image/png";
		case cmn::MediaCodecId::Webp:
			return "image/webp";
		default:
			return "application/octet-stream";
	}
}
