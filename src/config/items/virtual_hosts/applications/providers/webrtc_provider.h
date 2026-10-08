//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../common/cross_domain_support.h"
#include "provider.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct Rtx : public Item
				{
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enable)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxHoldMs, _max_hold_ms)

				protected:
					void MakeList() override
					{
						Register<Optional>("Enable", &_enable);
						Register<Optional>("MaxHoldMs", &_max_hold_ms);
					}

					bool _enable = false;
					int _max_hold_ms = 600;
				};

				struct WebrtcProvider : public Provider, public cmn::CrossDomainSupport
				{
					ProviderType GetType() const override
					{
						return ProviderType::WebRTC;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetTimeout, _timeout)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFIRInterval, _fir_interval)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRtcpBasedTimestamp, _rtcp_based_timestamp)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRtx, _rtx)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetStartBitrateHint, _start_bitrate_hint)

				protected:
					void MakeList() override
					{
						Provider::MakeList();

						Register<Optional>("Timeout", &_timeout);
						Register<Optional>("CrossDomains", &_cross_domains);
						Register<Optional>({"FIRInterval", "firInterval"}, &_fir_interval);
						Register<Optional>("RtcpBasedTimestamp", &_rtcp_based_timestamp);
						Register<Optional>("Rtx", &_rtx);
						Register<Optional>("StartBitrateHint", &_start_bitrate_hint);
					}

					int _timeout = 30000;
					int _fir_interval = 3000;
					bool _rtcp_based_timestamp = false;
					Rtx _rtx;
					// Sent to the WebRTC sender as x-google-start-bitrate in the SDP, in kbps, 0 leaves it out
					int _start_bitrate_hint = 1000;
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg