//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"
#include "base/mediarouter/mediarouter_application_interface.h"
#include "base/ovlibrary/url.h"
#include "base/publisher/publisher.h"
#include "hls_application.h"
#include "hls_http_interceptor.h"

class HlsPublisher : public pub::Publisher
{
public:
	static std::shared_ptr<HlsPublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

	HlsPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
	~HlsPublisher() override;
	bool Stop() override;

protected:
	bool PrepareHttpServers(
		const std::vector<ov::String> &ip_list,
		const bool is_port_configured, const uint16_t port,
		const bool is_tls_port_configured, const uint16_t tls_port,
		const int worker_count);

private:
	bool Start() override;

	//--------------------------------------------------------------------
	// Implementation of Publisher
	//--------------------------------------------------------------------
	PublisherType GetPublisherType() const override
	{
		return PublisherType::Hls;
	}
	const char *GetPublisherName() const override
	{
		return "HLSv3 Publisher";
	}

	bool OnCreateHost(const info::Host &host_info) override;
	bool OnDeleteHost(const info::Host &host_info) override;
	bool OnUpdateCertificate(const info::Host &host_info) override;
	std::shared_ptr<pub::Application> OnCreatePublisherApplication(const info::Application &application_info) override;
	bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) override;
	std::shared_ptr<TsHttpInterceptor> CreateInterceptor();

	std::mutex _http_server_list_mutex;
	std::vector<std::shared_ptr<http::svr::HttpServer>> _http_server_list;
	std::vector<std::shared_ptr<http::svr::HttpsServer>> _https_server_list;
};
