//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Jaejong Bong
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "module_template.h"

namespace cfg
{
	namespace modules
	{
		struct P2P : public ModuleTemplate
		{
		protected:
			int _max_client_peers_per_host_peer = 2;

		public:
			P2P(bool enable) : ModuleTemplate(enable)
			{
			}

			CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxClientPeersPerHostPeer, _max_client_peers_per_host_peer)

		protected:
			void MakeList() override
			{
				ModuleTemplate::MakeList();

				Register<Optional>("MaxClientPeersPerHostPeer", &_max_client_peers_per_host_peer);
			}
		};
	}  // namespace modules
}  // namespace cfg
