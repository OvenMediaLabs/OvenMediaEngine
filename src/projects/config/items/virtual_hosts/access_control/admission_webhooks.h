//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"
#include "enables.h"

namespace cfg
{
	namespace vhost
	{
		namespace sig
		{
			struct AdmissionWebhooks : public Item
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetControlServerUrl, _control_server_url)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetSecretKey, _secret_key)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetTimeoutMsec, _timeout_msec)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetEnabledProviders, _enables.GetProviders().GetValue())
				CFG_DECLARE_CONST_REF_GETTER_OF(GetEnabledPublishers, _enables.GetPublishers().GetValue())

				bool IsEnabledProvider(ProviderType type) const
				{
					auto type_str = StringFromProviderType(type);
					return _enables.GetProviders().IsExist(type_str);
				}

				bool IsEnabledPublisher(PublisherType type) const
				{
					auto type_str = StringFromPublisherType(type);
					return _enables.GetPublishers().IsExist(type_str);
				}

			protected:
				void MakeList() override
				{
					Register("ControlServerUrl", &_control_server_url);
					Register("SecretKey", &_secret_key);
					Register("Timeout", &_timeout_msec);
					Register("Enables", &_enables);
				}

				ov::String _control_server_url;
				ov::String _secret_key;
				int _timeout_msec = 3000;

				Enables _enables;
			};
		}  // namespace sig
	}  // namespace vhost
}  // namespace cfg
