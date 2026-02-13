//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "pass.h"

namespace cfg
{
	namespace vhost
	{
		namespace orgn
		{
			struct Origin : public Item
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetLocation, _location)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetPass, _pass)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsPersistent, _persistent)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsFailback, _failback)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsStrictLocation, _strict_location)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsRelay, _relay)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsRtcpSrTimestampIgnored, _ignore_rtcp_sr_timestamp)

			protected:
				void MakeList() override
				{
					Register("Location", &_location);
					Register("Pass", &_pass);
					Register<Optional>("Persistent", &_persistent);
					Register<Optional>("Failback", &_failback);
					Register<Optional>("StrictLocation", &_strict_location);
					Register<Optional>("Relay", &_relay);
					Register<Optional>("IgnoreRtcpSRTimestamp", &_ignore_rtcp_sr_timestamp);
				}
				ov::String _location;
				Pass _pass;
				bool _persistent = false;
				bool _failback = false;
				bool _strict_location = false;
				bool _relay = false;
				bool _ignore_rtcp_sr_timestamp = false;
			};
		}  // namespace orgn
	}	   // namespace vhost
}  // namespace cfg