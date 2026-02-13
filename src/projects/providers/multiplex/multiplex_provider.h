//==============================================================================
//
//  MultiplexProvider
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovlibrary/delay_queue.h>
#include <base/provider/provider.h>

namespace pvd
{
    class MultiplexProvider : public Provider
    {
    public:
        static std::shared_ptr<MultiplexProvider> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

        explicit MultiplexProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
        ~MultiplexProvider() override;

        bool Start() override;
        bool Stop() override;

        // Implementation of Provider's pure virtual functions
        ProviderStreamDirection GetProviderStreamDirection() const override
        {
            return ProviderStreamDirection::Push;
        }

        ProviderType GetProviderType() const override
        {
            return ProviderType::Multiplex;
        }

        const char* GetProviderName() const override
        {
            return "MultiplexProvider";
        }

        ocst::ModuleType GetModuleType() const override
        {
            return ocst::ModuleType::PushProvider;
        }

    private:
        bool OnCreateHost(const info::Host &host_info) override;
		bool OnDeleteHost(const info::Host &host_info) override;

        std::shared_ptr<pvd::Application> OnCreateProviderApplication(const info::Application &application_info) override;
        bool OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application) override;

        ov::DelayQueue _multiplex_watcher_timer{"AppTicker"};
    };
}