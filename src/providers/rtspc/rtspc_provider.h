//
// Created by getroot on 19. 12. 9.
//
#pragma once

#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/provider/pull_provider/application.h>
#include <base/provider/pull_provider/provider.h>
#include <orchestrator/orchestrator.h>

/*
 * RtspcProvider
 * 		: Create PhysicalPort, OvtApplication
 *
 * OvtApplication
 * 		: Create MediaRouterApplicationConnector, OvtStream
 *
 * OvtStream
 * 		: Create by interface (PullStream)
 * 		: Create Thread, the Thread has a Queue
 * 				: Communicate with OvtPublisher of Origin Server
 * 				: Send packets by RtspcProvider::PhysicalPort
 * 				: Receive packets from RtspcProvider -> OvtApplication -> OvtStream -> Queue
 *
 */

namespace pvd
{
	class RtspcProvider : public pvd::PullProvider
	{

	public:
		static std::shared_ptr<RtspcProvider> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		explicit RtspcProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		~RtspcProvider() override;

		ProviderStreamDirection GetProviderStreamDirection() const override
		{
			return ProviderStreamDirection::Pull;
		}

		ProviderType GetProviderType() const override
		{
			return ProviderType::RtspPull;
		}
		const char* GetProviderName() const override
	    {
		    return "RTSPCProvider";
	    }

		std::shared_ptr<ov::SocketPool> GetSignallingSocketPool();
	protected:
		bool OnCreateHost(const info::Host &host_info) override;
		bool OnDeleteHost(const info::Host &host_info) override;
		std::shared_ptr<pvd::Application> OnCreateProviderApplication(const info::Application &app_info) override;
		bool OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application) override;

		std::shared_ptr<ov::SocketPool> _signalling_socket_pool = nullptr;
		int _worker_count = 1;
		// Now, rtspc supports only interleaved channel (tcp based)
		std::shared_ptr<ov::SocketPool> _data_socket_pool;
	};
}  // namespace pvd