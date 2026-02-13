//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./stream.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				namespace mpegts
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
							Register<OmitJsonName>("Stream", &_stream_list);
						}
					};
				}  // namespace mpegts
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
