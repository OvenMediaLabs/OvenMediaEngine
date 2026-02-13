//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "provider.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct RtspPullProvider : public Provider
				{
					ProviderType GetType() const override
					{
						return ProviderType::RtspPull;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(IsBlockDuplicateStreamName, _is_block_duplicate_stream_name)

				protected:
					void MakeList() override
					{
						Provider::MakeList();

						Register<Optional>("BlockDuplicateStreamName", &_is_block_duplicate_stream_name);
					}

					// true: block(disconnect) new incoming stream
					// false: don't block new incoming stream
					bool _is_block_duplicate_stream_name = true;
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg