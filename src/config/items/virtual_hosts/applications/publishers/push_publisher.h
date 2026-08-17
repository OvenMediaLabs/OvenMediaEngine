//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../common/cross_domain_support.h"
#include "publisher.h"
#include "stream_map/stream_map.h"
namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct PushPublisher : public Publisher
				{
				public:
					PublisherType GetType() const override
					{
						return PublisherType::Push;
					}

				protected:
					pub::StreamMap _stream_map;
					int32_t _connection_timeout_ms = -1;
					int32_t _send_timeout_ms = -1;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamMap, _stream_map)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetConnectionTimeoutMs, _connection_timeout_ms)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSendTimeoutMs, _send_timeout_ms)
					
				protected:
					void MakeList() override
					{
						Register<Optional>({"StreamMap", "streamMap"}, &_stream_map);
						Register<Optional>({"ConnectionTimeout", "connectionTimeout"}, &_connection_timeout_ms);
						Register<Optional>({"SendTimeout", "sendTimeout"}, &_send_timeout_ms);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg