//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

/*
<Enable>true</Enable>
<ControlServerUrl>http://example.com/webhook</ControlServerUrl>
<SecretKey>abc123!@#</SecretKey>
<Timeout>1500</Timeout>
<UseLocalProfilesOnConnectionFailure>true</UseLocalProfilesOnConnectionFailure>
<UseLocalProfilesOnServerDisallow>false</UseLocalProfilesOnServerDisallow>
<UseLocalProfilesOnErrorResponse>false</UseLocalProfilesOnErrorResponse>
*/

#pragma once

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace trwh
			{
				struct TranscodeWebhook : public Item
				{
				protected:
					bool enabled = false;
					ov::String control_server_url;
					ov::String secret_key;
					int timeout									  = 1500;
					bool use_local_profiles_on_connection_failure = true;
					bool use_local_profiles_on_server_disallow	  = false;
					bool use_local_profiles_on_error_response	  = false;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, enabled)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetControlServerUrl, control_server_url)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSecretKey, secret_key)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTimeout, timeout)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetUseLocalProfilesOnConnectionFailure, use_local_profiles_on_connection_failure)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetUseLocalProfilesOnServerDisallow, use_local_profiles_on_server_disallow)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetUseLocalProfilesOnErrorResponse, use_local_profiles_on_error_response)

				protected:
					void MakeList() override
					{
						Register("Enable", &enabled);
						Register("ControlServerUrl", &control_server_url);
						Register("SecretKey", &secret_key);
						Register<Optional>("Timeout", &timeout);
						Register<Optional>("UseLocalProfilesOnConnectionFailure", &use_local_profiles_on_connection_failure);
						Register<Optional>("UseLocalProfilesOnServerDisallow", &use_local_profiles_on_server_disallow);
						Register<Optional>("UseLocalProfilesOnErrorResponse", &use_local_profiles_on_error_response);
					}
				};
			}  // namespace trwh
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
