//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <memory>

#include <base/info/application.h>
#include <base/mediarouter/mediarouter_interface.h>
#include <modules/ice/ice.h>
#include <modules/http/server/http_server.h>
#include <modules/http/server/https_server.h>
#include <modules/http/server/http_request_interceptors.h>
#include "../base/publisher/publisher.h"

class WebConsoleServer : public ov::EnableSharedFromThis<WebConsoleServer>
{
protected:
	struct PrivateToken {};
public:
	WebConsoleServer(const info::Host &application_info, PrivateToken token);
	~WebConsoleServer() override = default;

	static std::shared_ptr<WebConsoleServer> Create(const info::Host &host_info);

	bool Start(const ov::SocketAddress &address);
	bool Stop();

protected:
	bool InitializeServer();

	const info::Host &_host_info;
	cfg::vhost::app::wc::WebConsole _web_console;

	std::shared_ptr<http::svr::HttpServer> _http_server;
};
