//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "p2p.h"
#include "recovery.h"

namespace cfg
{
	namespace modules
	{
		struct modules : public Item
		{
		protected:
			ModuleTemplate _http2{true};
			ModuleTemplate _ll_hls{true};
			// Experimental feature disabled by default
			P2P _p2p{false};
			// Experimental feature disabled by default
			Recovery _recovery{false};
			ModuleTemplate _dynamic_app_removal{false};
			// Experimental feature is disabled by default
			ModuleTemplate _etag{false};
			// Experimental feature is disabled by default
			ModuleTemplate _ertmp{false};

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetHttp2, _http2)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetLLHls, _ll_hls)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetP2P, _p2p)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetRecovery, _recovery)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetDynamicAppRemoval, _dynamic_app_removal)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetETag, _etag)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetERTMP, _ertmp)

		protected:
			void MakeList() override
			{
				Register<Optional>("HTTP2", &_http2);
				Register<Optional>("LLHLS", &_ll_hls);
				Register<Optional>({"P2P", "p2p"}, &_p2p);
				Register<Optional>("Recovery", &_recovery);
				Register<Optional>("DynamicAppRemoval", &_dynamic_app_removal);
				Register<Optional>("ETag", &_etag);
				Register<Optional>("ERTMP", &_ertmp);
			}
		};
	}  // namespace modules
}  // namespace cfg
