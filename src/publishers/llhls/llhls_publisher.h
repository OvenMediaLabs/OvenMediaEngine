//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"
#include "base/mediarouter/mediarouter_application_interface.h"
#include "base/ovlibrary/url.h"
#include "base/publisher/publisher.h"
#include "llhls_application.h"
#include "llhls_http_interceptor.h"

class LLHlsPublisher : public pub::Publisher
{
public:
	static std::shared_ptr<LLHlsPublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

	LLHlsPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
	~LLHlsPublisher() override;
	bool Start() override;
	bool Stop() override;

protected:
	bool PrepareHttpServers(
		const std::vector<ov::String> &ip_list,
		const bool is_port_configured, const uint16_t port,
		const bool is_tls_port_configured, const uint16_t tls_port,
		const int worker_count);

private:
	//--------------------------------------------------------------------
	// Implementation of Publisher
	//--------------------------------------------------------------------
	PublisherType GetPublisherType() const override
	{
		return PublisherType::LLHls;
	}
	const char *GetPublisherName() const override
	{
		return "LLHLS Publisher";
	}

	bool OnCreateHost(const info::Host &host_info) override;
	bool OnDeleteHost(const info::Host &host_info) override;
	bool OnUpdateCertificate(const info::Host &host_info) override;
	std::shared_ptr<pub::Application> OnCreatePublisherApplication(const info::Application &application_info) override;
	bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) override;
	std::shared_ptr<LLHlsHttpInterceptor> CreateInterceptor();

	// Sends the closing request on a worker, since nothing waits for its answer
	void PostCloseAdmissionWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info);
	// The open session this connection started with a request for the same URL
	std::shared_ptr<LLHlsSession> FindSessionOpenedBy(const std::shared_ptr<http::svr::HttpConnection> &connection, const std::shared_ptr<const ov::Url> &requested_url);
	// Closes the session once it has stayed without a connection for the grace period
	void CloseSessionAfterGrace(const std::shared_ptr<LLHlsSession> &session, uint64_t idle_since, const std::shared_ptr<const ov::Socket> &last_socket);
	void CloseSessionIfIdleSince(const std::shared_ptr<LLHlsSession> &session, uint64_t idle_since, const std::shared_ptr<const ov::Socket> &last_socket);

	// A session is closed and taken out of its stream as one step, so a URL cannot re-create it in between
	std::mutex _session_lifecycle_mutex;

	std::mutex _http_server_list_mutex;
	std::vector<std::shared_ptr<http::svr::HttpServer>> _http_server_list;
	std::vector<std::shared_ptr<http::svr::HttpsServer>> _https_server_list;
};
