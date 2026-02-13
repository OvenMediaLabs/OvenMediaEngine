//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../common/cross_domain_support.h"
#include "../common/host/host.h"
#include "access_control/admission_webhooks.h"
#include "access_control/signed_policy.h"
#include "applications/applications.h"
#include "origin_map_store/origin_map_store.h"
#include "origins/origins.h"

namespace cfg
{
	namespace vhost
	{
		struct VirtualHost : public Item, public cmn::CrossDomainSupport
		{
		protected:
			ov::String _name;
			ov::String _distribution = "ovenmediaengine.com";

			cmn::Host _host;
			sig::SignedPolicy _signed_policy;
			sig::AdmissionWebhooks _admission_webhooks;
			orgn::Origins _origins;
			orgn::OriginMapStore _origin_map_store;
			app::Applications _applications;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetDistribution, _distribution)

			CFG_DECLARE_CONST_REF_GETTER_OF(GetHost, _host)

			CFG_DECLARE_CONST_REF_GETTER_OF(GetSignedPolicy, _signed_policy)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetAdmissionWebhooks, _admission_webhooks)

			CFG_DECLARE_CONST_REF_GETTER_OF(GetOrigins, _origins)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetOriginList, _origins.GetOriginList())
			CFG_DECLARE_CONST_REF_GETTER_OF(GetOriginMapStore, _origin_map_store)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetApplicationList, _applications.GetApplicationList())

			bool GetApplicationByName(ov::String name, cfg::vhost::app::Application &application) const
			{
				auto &app_list = GetApplicationList();
				for (auto &item : app_list)
				{
					if (item.GetName() == name)
					{
						application = item;
						return true;
					}
				}

				return false;
			}

		protected:
			void MakeList() override
			{
				Register("Name", &_name);
				Register<Optional>("Distribution", &_distribution);

				Register<Optional>("Host", &_host);

				Register<Optional>("SignedPolicy", &_signed_policy);
				Register<Optional>("AdmissionWebhooks", &_admission_webhooks);

				Register<Optional>("Origins", &_origins);
				Register<Optional>("OriginMapStore", &_origin_map_store);

				// Deprecated: Use CrossDomains in Server instead.
				Register<Optional>("CrossDomains", &_cross_domains, nullptr, [=]() -> std::shared_ptr<ConfigError> {
					logw("Config", "CrossDomains in VirtualHost is deprecated. Use default CrossDomains in Server instead.");
					return nullptr;
				});

				Register<Optional>("Applications", &_applications);
			}
		};
	}  // namespace vhost
}  // namespace cfg
