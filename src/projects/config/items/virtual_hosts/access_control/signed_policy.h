//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
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
			struct SignedPolicy : public Item
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetPolicyQueryKeyName, _policy_query_key_name)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetSignatureQueryKeyName, _signature_query_key_name)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetSecretKey, _secret_key)
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
					Register("PolicyQueryKeyName", &_policy_query_key_name);
					Register("SignatureQueryKeyName", &_signature_query_key_name);
					Register("SecretKey", &_secret_key);
					Register("Enables", &_enables);
				}

				ov::String _policy_query_key_name;
				ov::String _signature_query_key_name;
				ov::String _secret_key;

				Enables _enables;
			};
		}  // namespace sig
	}  // namespace vhost
}  // namespace cfg
