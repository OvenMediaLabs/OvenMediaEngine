//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "persistent_stream.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace prst
			{
				struct PersistentStreams : public Item
				{
				protected:
					std::vector<Stream> _streams;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetStreams, _streams);

				protected:
					void MakeList() override
					{
						Register<Optional>("Stream", &_streams);
					}
				};
			}  // namespace prst
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
