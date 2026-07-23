//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================

#include "web_console_server.h"
#include "web_console_private.h"

WebConsoleServer::WebConsoleServer(const info::Host &host_info, PrivateToken token)
	: _host_info(host_info)
{
}

std::shared_ptr<WebConsoleServer> WebConsoleServer::Create(const info::Host &host_info)
{
	auto instance = std::make_shared<WebConsoleServer>(host_info, (PrivateToken){});

	if (instance != nullptr)
	{
		// const auto &web_console = host_info.GetListen().GetWebConsole();
		cfg::vhost::app::wc::WebConsole web_console;

		// TODO(dimiden): Implement web console
		OV_ASSERT2(false);
		// instance->_web_console = web_console;

		// auto host = web_console.GetParentAs<cfg::vhost::VirtualHost>("VirtualHost");
		// auto address = ov::SocketAddress(host->GetIp(), static_cast<uint16_t>(web_console.GetListenPort()));

		// logti("Trying to start WebConsole on %s...", address.ToString().CStr());

		// if (instance->Start(address))
		// {
		// 	return instance;
		// }
		// else
		// {
		// 	logte("Could not start WebConsole");
		// }
	}
	else
	{
		logte("Could not initialize WebConsole");
	}

	logte("An error occurred while creating WebConsoleServer");
	return nullptr;
}

bool WebConsoleServer::Start(const ov::SocketAddress &address)
{
	if (_http_server != nullptr)
	{
		OV_ASSERT(false, "Server is already running");
		return false;
	}

/* TODO(Dimiden): http::svr::HttpsServer has changed to add certificate. 
	auto certificate = _host_info.GetCertificate();
	if (certificate != nullptr)
	{
		auto https_server = std::make_shared<http::svr::HttpsServer>();

		https_server->SetLocalCertificate(certificate);
		https_server->SetChainCertificate(_host_info.GetChainCertificate());

		_http_server = https_server;
	}
	else
	{
		_http_server = std::make_shared<http::svr::Server>();
	}
*/
	//_http_server = std::make_shared<http::svr::Server>();
//
	//return InitializeServer() && _http_server->Start(address);

	return false;
}

bool WebConsoleServer::Stop()
{
	if (_http_server == nullptr)
	{
		return false;
	}

	if (_http_server->Stop())
	{
		_http_server = nullptr;

		return true;
	}

	return false;
}

bool WebConsoleServer::InitializeServer()
{
	auto http_interceptor = std::make_shared<http::svr::DefaultInterceptor>();
	ov::String document_root = ov::PathManager::GetCanonicalPath(_web_console.GetDocumentPath());

	http_interceptor->Register(http::Method::Post, "/api/login", [](const std::shared_ptr<http::svr::HttpExchange> &client) -> http::svr::NextHandler {
		// auto request_body = request->GetRequestBody();
		// auto json = ov::Json::Parse(request_body);

		return http::svr::NextHandler::DoNotCall;
	});

	http_interceptor->Register(http::Method::Post, "/api/logout", [](const std::shared_ptr<http::svr::HttpExchange> &client) -> http::svr::NextHandler {
		// auto request_body = request->GetRequestBody();
		// auto json = ov::Json::Parse(request_body);

		// logti("BODY: %s", json.ToString().CStr());

		return http::svr::NextHandler::DoNotCall;
	});

	http_interceptor->Register(http::Method::Get, ".*", [document_root](const std::shared_ptr<http::svr::HttpExchange> &exchange) -> http::svr::NextHandler {
		auto request = exchange->GetRequest();
		auto response = exchange->GetResponse();

		auto path = ov::PathManager::Combine(document_root, request->GetRequestTarget());
		auto real_path = ov::PathManager::GetCanonicalPath(path);

		if (real_path.IsEmpty())
		{
			response->SetStatusCode(http::StatusCode::NotFound);
			response->Response();

			exchange->Release();

			return http::svr::NextHandler::DoNotCall;
		}

		if (real_path.HasPrefix(document_root) == false)
		{
			// If the file exists but is not in the DocumentRoot, it is not provided because of the risk of attack
			logtw("The client requested a resource, but the resource not in DocumentRoot: %s", real_path.CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			response->Response();

			exchange->Release();

			return http::svr::NextHandler::DoNotCall;
		}

		FILE *file = ::fopen(real_path, "rb");

		if (file == nullptr)
		{
			response->SetStatusCode(http::StatusCode::NotFound);
			response->Response();

			exchange->Release();

			return http::svr::NextHandler::DoNotCall;
		}

		auto data = std::make_shared<ov::Data>();
		ov::ByteStream stream(data.get());
		char buffer[4096];

		while (!feof(file))
		{
			auto read_bytes = ::fread(buffer, 1, OV_COUNTOF(buffer), file);

			if (read_bytes == 0)
			{
				break;
			}

			stream.Append(buffer, read_bytes);
		}

		if (response->GetStatusCode() == http::StatusCode::OK)
		{
			response->SetHeader("Content-Length", ov::String::FormatString("%zu", data->GetLength()));
			response->SetHeader("Content-Type", "text/html");
			response->AppendData(data);
		}

		::fclose(file);

		response->Response();

		exchange->Release();

		return http::svr::NextHandler::DoNotCall;
	});

	return _http_server->AddInterceptor(http_interceptor);
}
