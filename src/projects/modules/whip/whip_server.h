//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/http/http.h>

#include "whip_interceptor.h"
#include "whip_observer.h"

class WhipServer : public ov::EnableSharedFromThis<WhipServer>
{
public:
	WhipServer(const cfg::bind::cmm::Webrtc &webrtc_bind_cfg);

	bool Start(
		const std::shared_ptr<WhipObserver> &observer,
		const char *server_name, const char *server_short_name,
		const std::vector<ov::String> &ip_list,
		bool is_port_configured, uint16_t port,
		bool is_tls_port_configured, uint16_t tls_port,
		int worker_count);
	bool Stop();

	bool InsertCertificate(const std::shared_ptr<const info::Certificate> &certificate);
	bool RemoveCertificate(const std::shared_ptr<const info::Certificate> &certificate);

	void SetCors(const info::VHostAppName &vhost_app_name, const cfg::cmn::CrossDomains &cross_domain_cfg);

protected:
	struct TurnIP
	{
		ov::SocketFamily family;
		ov::String ip;

		TurnIP(const ov::SocketAddress &address)
			: family(address.GetFamily()),
			  ip(address.GetIpAddress())
		{
		}

		TurnIP(const ov::SocketFamily family, const ov::String &ip)
			: family(family),
			  ip(ip)
		{
		}

		static std::vector<TurnIP> FromIPList(const ov::SocketFamily family, const std::vector<ov::String> &ip_list)
		{
			std::vector<TurnIP> turn_ip_list;

			for (const auto &ip : ip_list)
			{
				turn_ip_list.emplace_back(TurnIP(family, ip));
			}

			return turn_ip_list;
		}
	};

protected:
	bool PrepareForTCPRelay();
	bool PrepareForExternalIceServer();

private:
	std::shared_ptr<WhipInterceptor> CreateInterceptor();
	ov::String GetIceServerLinkValue(const ov::String &URL, const ov::String &username, const ov::String &credential);

	const cfg::bind::cmm::Webrtc _webrtc_bind_cfg;

	std::shared_ptr<WhipObserver> _observer;

	std::recursive_mutex _http_server_list_mutex;
	std::vector<std::shared_ptr<http::svr::HttpServer>> _http_server_list;
	std::vector<std::shared_ptr<http::svr::HttpsServer>> _https_server_list;

	std::set<ov::String> _link_headers;
	bool _tcp_force = false;

	http::CorsManager _cors_manager;

	ov::SocketAddress::Address _tcp_relay_address;
};