//
// Created by getroot on 19. 12. 9.
//

#include <base/ovlibrary/url.h>
#include "rtspc_provider.h"
#include "rtspc_application.h"
#include "rtspc_stream.h"

#define OV_LOG_TAG "RtspcProvider"

using namespace cmn;

namespace pvd
{
	std::shared_ptr<RtspcProvider> RtspcProvider::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto provider = std::make_shared<RtspcProvider>(server_config, router);
		if (!provider->Start())
		{
			logte("An error occurred while creating RtspcProvider");
			return nullptr;
		}
		return provider;
	}

	RtspcProvider::RtspcProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
			: PullProvider(server_config, router)
	{
		auto &rtspc_provider_config = server_config.GetBind().GetProviders().GetRtspc();

		bool is_parsed;
		auto _worker_count = rtspc_provider_config.GetWorkerCount(&is_parsed);
		_worker_count = is_parsed ? _worker_count : PHYSICAL_PORT_DEFAULT_WORKER_COUNT;
	}

	RtspcProvider::~RtspcProvider()
	{
		Stop();

		if (_signalling_socket_pool != nullptr)
		{
			_signalling_socket_pool->Uninitialize();
		}

		logtt("Terminated Rtspc Provider modules.");
	}

	std::shared_ptr<ov::SocketPool> RtspcProvider::GetSignallingSocketPool()
	{
		if(_signalling_socket_pool == nullptr)
		{
			_signalling_socket_pool = ov::SocketPool::Create("RtspcProvider", ov::SocketType::Tcp, false);
			_signalling_socket_pool->Initialize(_worker_count);
		}

		return _signalling_socket_pool;
	}

	bool RtspcProvider::OnCreateHost(const info::Host &host_info)
	{
		return true;
	}
	
	bool RtspcProvider::OnDeleteHost(const info::Host &host_info)
	{
		return true;
	}

	std::shared_ptr<pvd::Application> RtspcProvider::OnCreateProviderApplication(const info::Application &app_info)
	{
		if(IsModuleAvailable() == false)
		{
			return nullptr;
		}

		return RtspcApplication::Create(GetSharedPtrAs<pvd::PullProvider>(), app_info);
	}

	bool RtspcProvider::OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application)
	{
		return true;
	}
}