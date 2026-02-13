#pragma once

#include <modules/http/server/http_server_manager.h>
#include <orchestrator/orchestrator.h>

#include "base/common_types.h"
#include "base/info/record.h"
#include "base/mediarouter/mediarouter_application_interface.h"
#include "base/ovlibrary/url.h"
#include "base/publisher/publisher.h"
#include "thumbnail_application.h"
#include "thumbnail_interceptor.h"

class ThumbnailPublisher : public pub::Publisher
{
public:
	static std::shared_ptr<ThumbnailPublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

	ThumbnailPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
	~ThumbnailPublisher() override;
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
		return PublisherType::Thumbnail;
	}
	const char *GetPublisherName() const override
	{
		return "ThumbnailPublisher";
	}

	bool OnCreateHost(const info::Host &host_info) override;
	bool OnDeleteHost(const info::Host &host_info) override;
	bool OnUpdateCertificate(const info::Host &host_info) override;
	std::shared_ptr<pub::Application> OnCreatePublisherApplication(const info::Application &application_info) override;
	bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) override;

	static ov::String MimeTypeFromMediaCodecId(const cmn::MediaCodecId &type);
	
private:
	std::shared_ptr<ThumbnailInterceptor> CreateInterceptor();

	std::mutex _http_server_list_mutex;
	std::vector<std::shared_ptr<http::svr::HttpServer>> _http_server_list;
	std::vector<std::shared_ptr<http::svr::HttpsServer>> _https_server_list;
};
