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

	ov::RecursiveMutex _http_server_list_mutex;
	std::vector<std::shared_ptr<http::svr::HttpServer>> _http_server_list OV_GUARDED_BY(_http_server_list_mutex);
	std::vector<std::shared_ptr<http::svr::HttpsServer>> _https_server_list OV_GUARDED_BY(_http_server_list_mutex);

	// Cached certificates that must be applied to every HTTPS server in `_https_server_list`.
	// `InsertCertificate()` can be called before `Start()` constructs the HTTPS servers
	// (e.g., during the `OnCreateHost()` notification pass between `RegisterModule()` and the provider's later `Bind()`);
	// without this cache those early inserts would be silently dropped against an empty server list.
	// Protected by `_http_server_list_mutex`.
	//
	// Lock order: this mutex (outer) -> `HttpsServer::_https_certificate_map_mutex` (inner).
	std::map<ov::String, std::shared_ptr<const info::Certificate>> _certificate_map OV_GUARDED_BY(_http_server_list_mutex);

	std::set<ov::String> _link_headers;
	bool _tcp_force = false;
	ov::String _default_transport{"UDPTCP"};

	http::CorsManager _cors_manager;

	ov::SocketAddress::Address _tcp_relay_address;
};