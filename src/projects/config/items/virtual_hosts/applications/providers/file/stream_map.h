//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stream.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				namespace file
				{
					struct StreamMap : public Item
					{
					protected:
						std::vector<Stream> _stream_list;

					public:
						CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamList, _stream_list)

					protected:
						void MakeList() override
						{
							Register<Optional>("Stream", &_stream_list);
						}
					};
				}  // namespace file
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
