//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "whip_server.h"

#include <modules/address/address_utilities.h>

#include "whip_interceptor.h"
#include "whip_private.h"

WhipServer::WhipServer(const cfg::bind::cmm::Webrtc &webrtc_bind_cfg)
	: _webrtc_bind_cfg(webrtc_bind_cfg)
{
}

bool WhipServer::PrepareForTCPRelay()
{
	// For internal TURN/TCP relay configuration
	const auto &ice_candidates_config = _webrtc_bind_cfg.GetIceCandidates();
	_tcp_force = ice_candidates_config.IsTcpForce();

	bool is_tcp_relay_configured = false;
	const auto &tcp_relay_list = ice_candidates_config.GetTcpRelayList(&is_tcp_relay_configured);

	if (is_tcp_relay_configured)
	{
		for (auto &tcp_relay : tcp_relay_list)
		{
			// <TcpRelay>IP:Port</TcpRelay>
			// <TcpRelay>*:Port</TcpRelay>
			// <TcpRelay>[::]:Port</TcpRelay>
			// <TcpRelay>${PublicIP}:Port</TcpRelay>

			// Check whether tcp_relay_address indicates "any address"(*, ::) or ${PublicIP}
			_tcp_relay_address = ov::SocketAddress::ParseAddress(tcp_relay);
			if (_tcp_relay_address.HasPortList() == false)
			{
				logte("Invalid TCP relay address: %s (The TCP relay address must be in <IP>:<Port> format)", tcp_relay.CStr());
				return false;
			}

			auto address_utilities = ov::AddressUtilities::GetInstance();

			std::vector<TurnIP> ip_list;
			std::vector<ov::String> url_list;

			auto &tcp_relay_host = _tcp_relay_address.host;
			if (tcp_relay_host == OV_SOCKET_WILDCARD_IPV4)
			{
				// Case 1 - IPv4 wildcard
				ip_list = TurnIP::FromIPList(ov::SocketFamily::Inet, address_utilities->GetIPv4List());
			}
			else if (tcp_relay_host == OV_SOCKET_WILDCARD_IPV6)
			{
				// Case 2 - IPv6 wildcard
				ip_list = TurnIP::FromIPList(ov::SocketFamily::Inet6, address_utilities->GetIPv6List(ice_candidates_config.GetEnableLinkLocalAddress()));
			}
			else if (tcp_relay_host == OV_ICE_PORT_PUBLIC_IP)
			{
				const auto public_ip_list = address_utilities->GetMappedAddressList();

				if (public_ip_list.empty() == false)
				{
					// Case 3 - Get an IP from external STUN server
					for (const auto &public_ip : public_ip_list)
					{
						ip_list.emplace_back(public_ip);
					}
				}
				else
				{
					// Case 4 - Could not obtain an IP from the STUN server
					logtw(OV_ICE_PORT_PUBLIC_IP " is specified on TCP relay, but failed to obtain public IP: %s", tcp_relay.CStr());
				}
			}
			else
			{
				// Case 5 - Use the domain as it is
				url_list.emplace_back(ov::String::FormatString("turn:%s?transport=tcp", tcp_relay.CStr()));
			}

			for (const auto &ip : ip_list)
			{
				_tcp_relay_address.EachPort([&](const ov::String &host, const uint16_t port) -> bool {
					if (ip.family == ov::SocketFamily::Inet6)
					{
						url_list.emplace_back(ov::String::FormatString("turn:[%s]:%d?transport=tcp", ip.ip.CStr(), port));
					}
					else
					{
						url_list.emplace_back(ov::String::FormatString("turn:%s:%d?transport=tcp", ip.ip.CStr(), port));
					}
					return true;
				});
			}

			for (const auto &url : url_list)
			{
				_link_headers.emplace(GetIceServerLinkValue(url, DEFAULT_RELAY_USERNAME, DEFAULT_RELAY_KEY));
			}
		}
	}

	return true;
}

bool WhipServer::PrepareForExternalIceServer()
{
	// for external ice server configuration
	auto &ice_servers_config = _webrtc_bind_cfg.GetIceServers();
	if (ice_servers_config.IsParsed())
	{
		for (auto ice_server_config : ice_servers_config.GetIceServerList())
		{
			ov::String username, credential;

			// UserName
			if (ice_server_config.GetUserName().IsEmpty() == false)
			{
				// "user_name" is out of specification. This is a bug and "username" is correct. "user_name" will be deprecated in the future.
				username = ice_server_config.GetUserName();
			}

			// Credential
			if (ice_server_config.GetCredential().IsEmpty() == false)
			{
				credential = ice_server_config.GetCredential();
			}

			// URLS
			auto &url_list = ice_server_config.GetUrls().GetUrlList();
			if (url_list.size() == 0)
			{
				logtw("There is no URL list in ICE Servers");
				continue;
			}

			for (auto url : url_list)
			{
				auto address = ov::String::FormatString("turn:%s?transport=tcp", url.CStr());
				_link_headers.emplace(GetIceServerLinkValue(address, username, credential));
			}
		}
	}

	return true;
}

bool WhipServer::Start(
	const std::shared_ptr<WhipObserver> &observer,
	const char *server_name, const char *server_short_name,
	const std::vector<ov::String> &ip_list,
	bool is_port_configured, uint16_t port,
	bool is_tls_port_configured, uint16_t tls_port,
	int worker_count)
{
	if ((_http_server_list.empty() == false) || (_https_server_list.empty() == false))
	{
		OV_ASSERT(false, "%s is already running (%zu, %zu)",
				  server_name,
				  _http_server_list.size(),
				  _https_server_list.size());
		return false;
	}

	auto interceptor = CreateInterceptor();

	if (interceptor == nullptr)
	{
		logte("Could not create interceptor");
		return false;
	}

	_observer = observer;

	auto http_server_manager = http::svr::HttpServerManager::GetInstance();

	std::vector<std::shared_ptr<http::svr::HttpServer>> http_server_list;
	std::vector<std::shared_ptr<http::svr::HttpsServer>> https_server_list;

	if (http_server_manager->CreateServers(
			server_name, server_short_name,
			&http_server_list, &https_server_list,
			ip_list,
			is_port_configured, port,
			is_tls_port_configured, tls_port,
			nullptr, false,
			[&](const ov::SocketAddress &address, bool is_https, const std::shared_ptr<http::svr::HttpServer> &http_server) {
				http_server->AddInterceptor(interceptor);
			},
			worker_count))
	{
		if (PrepareForTCPRelay() && PrepareForExternalIceServer())
		{
			std::lock_guard lock_guard{_http_server_list_mutex};
			_http_server_list = std::move(http_server_list);
			_https_server_list = std::move(https_server_list);

			return true;
		}

		logte("Could not prepare TCP relay. Uninitializing RtcSignallingServer...");
	}

	http_server_manager->ReleaseServers(&http_server_list);
	http_server_manager->ReleaseServers(&https_server_list);

	return false;
}

bool WhipServer::Stop()
{
	_observer = nullptr;

	return true;
}

bool WhipServer::InsertCertificate(const std::shared_ptr<const info::Certificate> &certificate)
{
	if (certificate != nullptr)
	{
		_http_server_list_mutex.lock();
		auto https_server_list = _https_server_list;
		_http_server_list_mutex.unlock();

		for (auto &https_server : https_server_list)
		{
			auto error = https_server->InsertCertificate(certificate);
			if (error != nullptr)
			{
				logte("Could not append certificate to %p: %s", https_server.get(), error->What());
				return false;
			}
		}
	}

	return true;
}

bool WhipServer::RemoveCertificate(const std::shared_ptr<const info::Certificate> &certificate)
{
	_http_server_list_mutex.lock();
	auto https_server_list = _https_server_list;
	_http_server_list_mutex.unlock();

	for (auto &https_server : https_server_list)
	{
		auto error = https_server->RemoveCertificate(certificate);
		if (error != nullptr)
		{
			logte("Could not remove certificate from %p: %s", https_server.get(), error->What());
			return false;
		}
	}

	return true;
}

void WhipServer::SetCors(const info::VHostAppName &vhost_app_name, const cfg::cmn::CrossDomains &cross_domain_cfg)
{
	_cors_manager.SetCrossDomains(vhost_app_name, cross_domain_cfg);
}

ov::String WhipServer::GetIceServerLinkValue(const ov::String &URL, const ov::String &username, const ov::String &credential)
{
	// <turn:turn.example.net?transport=tcp>; rel="ice-server"; username="user"; credential="myPassword"; credential-type="password"
	return ov::String::FormatString("<%s>; rel=\"ice-server\"; username=\"%s\"; credential=\"%s\"; credential-type=\"password\"", URL.CStr(), username.CStr(), credential.CStr());
}

std::shared_ptr<WhipInterceptor> WhipServer::CreateInterceptor()
{
	auto interceptor = std::make_shared<WhipInterceptor>();

	// OPTION
	interceptor->Register(http::Method::Options, R"([\s\S]*)", [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
		auto connection = exchange->GetConnection();
		auto request = exchange->GetRequest();
		auto response = exchange->GetResponse();

		logti("WHIP Requested: %s", request->ToString().CStr());

		auto request_url = request->GetParsedUri();
		if (request_url == nullptr)
		{
			logte("Could not parse request url: %s", request->GetUri().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(request_url->Host(), request_url->App());
		if (vhost_app_name.IsValid() == false)
		{
			logte("Could not resolve application name from domain: %s", request_url->Host().CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			return http::svr::NextHandler::DoNotCall;
		}

		_cors_manager.SetupHttpCorsHeader(vhost_app_name, request, response, {http::Method::Options, http::Method::Post, http::Method::Patch, http::Method::Delete});

		response->SetStatusCode(http::StatusCode::OK);
		response->SetHeader("Access-Control-Allow-Private-Network", "true");

		return http::svr::NextHandler::DoNotCall;
	});

	// POST
	interceptor->Register(http::Method::Post, R"([\s\S]*)", [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
		auto connection = exchange->GetConnection();
		auto request = exchange->GetRequest();
		auto response = exchange->GetResponse();

		logti("WHIP Requested: %s", request->ToString().CStr());

		if (_observer == nullptr)
		{
			logte("Internal Server Error - Observer is not set");
			response->SetStatusCode(http::StatusCode::InternalServerError);
			return http::svr::NextHandler::DoNotCall;
		}

		// Check if Content-Type is application/sdp
		auto content_type = request->GetHeader("Content-Type");
		if (content_type.IsEmpty() || content_type != "application/sdp")
		{
			logtw("Content-Type is not application/sdp: %s", content_type.CStr());
		}

		auto requested_url = request->GetParsedUri();
		if (requested_url == nullptr)
		{
			logte("Could not parse request url: %s", request->GetUri().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto data = request->GetRequestBody();
		if (data == nullptr)
		{
			logte("Could not get request body");
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		logti("WHIP SDP Offer: %s", data->ToString().CStr());

		auto offer_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer);
		if (offer_sdp->FromString(data->ToString()) == false)
		{
			logte("Could not parse SDP: %s", data->ToString().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto answer = _observer->OnSdpOffer(request, offer_sdp);
		response->SetStatusCode(answer._status_code);

		if (answer._status_code == http::StatusCode::Created)
		{
			auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationName(answer._vhost_name, answer._app_name);
			if (vhost_app_name.IsValid() == false)
			{
				logte("Could not resolve application name from domain: %s", answer._vhost_name.CStr());
				response->SetStatusCode(http::StatusCode::NotFound);
				return http::svr::NextHandler::DoNotCall;
			}

			// Set CORS header in response
			_cors_manager.SetupHttpCorsHeader(vhost_app_name, request, response, {http::Method::Post});

			// Access-Control-Expose-Headers header allows a server
			// to indicate which response headers should be made available to scripts running in the browser in response to a cross-origin request.
			response->AddHeader("Access-Control-Expose-Headers", "Location, Link, ETag");

			// Set SDP
			response->SetHeader("Content-Type", "application/sdp");
			response->SetHeader("ETag", answer._entity_tag);
			response->SetHeader("Location", ov::String::FormatString("/%s/%s/%s?direction=whip", requested_url->App().CStr(), requested_url->Stream().CStr(), answer._session_id.CStr()));

			// IF TcpForce == true or ?transport=tcp
			if (_tcp_force == true || requested_url->GetQueryValue("transport").UpperCaseString() == "TCP")
			{
				// Add ICE Server Link

				// Add ICE Server Link from configuration
				for (const auto &ice_server : _link_headers)
				{
					// Multiple Link headers are allowed
					response->AddHeader("Link", ice_server);
				}
			}

			response->AppendString(answer._sdp->ToString());
		}
		else
		{
			// Set Error
			if (answer._error_message.IsEmpty() == false)
			{
				response->SetHeader("Content-Type", "text/plain");
				response->AppendString(answer._error_message);
			}
		}

		logti("WHIP Response: %s", response->ToString().CStr());

		return http::svr::NextHandler::DoNotCall;
	});

	// DELETE
	interceptor->Register(http::Method::Delete, R"([\s\S]*)", [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
		auto connection = exchange->GetConnection();
		auto request = exchange->GetRequest();
		auto response = exchange->GetResponse();

		logti("WHIP Requested: %s", request->ToString().CStr());

		if (_observer == nullptr)
		{
			logte("Internal Server Error - Observer is not set");
			response->SetStatusCode(http::StatusCode::InternalServerError);
			return http::svr::NextHandler::DoNotCall;
		}

		auto final_url = request->GetParsedUri();
		if (final_url == nullptr)
		{
			logte("Could not parse request url: %s", request->GetUri().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto session_key = final_url->File();
		if (session_key.IsEmpty())
		{
			logte("Could not get session key from url: %s", final_url->ToUrlString(true).CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto answer = _observer->OnSessionDelete(request, session_key);
		response->SetStatusCode(answer._status_code);

		if (answer._status_code == http::StatusCode::OK)
		{
			auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(answer._vhost_name, answer._app_name);
			if (vhost_app_name.IsValid() == false)
			{
				logte("Could not resolve application name from domain: %s", answer._vhost_name.CStr());
				response->SetStatusCode(http::StatusCode::NotFound);
				return http::svr::NextHandler::DoNotCall;
			}

			// Set CORS header in response
			_cors_manager.SetupHttpCorsHeader(vhost_app_name, request, response);
		}

		return http::svr::NextHandler::DoNotCall;
	});

	// PATCH
	interceptor->Register(http::Method::Patch, R"([\s\S]*)", [this](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
		auto connection = exchange->GetConnection();
		auto request = exchange->GetRequest();
		auto response = exchange->GetResponse();

		logti("WHIP Requested: %s", request->ToString().CStr());

		if (_observer == nullptr)
		{
			logte("Internal Server Error - Observer is not set");
			response->SetStatusCode(http::StatusCode::InternalServerError);
			return http::svr::NextHandler::DoNotCall;
		}

		// Check if Content-Type is application/trickle-ice-sdpfrag
		auto content_type = request->GetHeader("Content-Type");
		if (content_type.IsEmpty() || content_type != "application/trickle-ice-sdpfrag")
		{
			logte("Content-Type is not application/trickle-ice-sdpfrag");
		}

		auto request_url = request->GetParsedUri();
		if (request_url == nullptr)
		{
			logte("Could not parse request url: %s", request->GetUri().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(request_url->Host(), request_url->App());
		if (vhost_app_name.IsValid() == false)
		{
			logte("Could not resolve application name from domain: %s", request_url->Host().CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			return http::svr::NextHandler::DoNotCall;
		}

		auto stream_name = request_url->Stream();

		//TODO(way) : If url is changed by Webhooks, we use the changed url in PATCH request like delete request.

		// Set CORS header in response
		_cors_manager.SetupHttpCorsHeader(vhost_app_name, request, response, {http::Method::Patch});

		auto session_id = request_url->File();
		if (session_id.IsEmpty())
		{
			logte("Could not get session id from url");
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto if_match = request->GetHeader("If-Match");

		auto data = request->GetRequestBody();
		if (data == nullptr)
		{
			logte("Could not get request body");
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		logti("Received PATCH request: %s", data->ToString().CStr());

		// Parse SDP
		auto patch_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Update);
		if (patch_sdp->FromString(data->ToString()) == false)
		{
			logte("Could not parse SDP: %s", data->ToString().CStr());
			response->SetStatusCode(http::StatusCode::BadRequest);
			return http::svr::NextHandler::DoNotCall;
		}

		auto answer = _observer->OnTrickleCandidate(request, session_id, if_match, patch_sdp);
		response->SetStatusCode(answer._status_code);
		response->SetHeader("Content-Type", "application/trickle-ice-sdpfrag");

		if (answer._entity_tag.IsEmpty() == false)
		{
			response->SetHeader("ETag", answer._entity_tag);
		}

		if (answer._status_code == http::StatusCode::OK)
		{
			response->AppendString(answer._sdp->ToString());
		}
		else
		{
			// Set Error
			if (answer._error_message.IsEmpty() == false)
			{
				response->SetHeader("Content-Type", "text/plain");
				response->AppendString(answer._error_message);
			}
		}

		return http::svr::NextHandler::DoNotCall;
	});

	return interceptor;
}