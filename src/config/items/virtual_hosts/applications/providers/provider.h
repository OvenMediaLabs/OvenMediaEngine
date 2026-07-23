//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct Provider : public Item
				{
				protected:
					int _max_connection			  = 0;
					TimestampMode _timestamp_mode = TimestampMode::Auto;
					bool _use_incoming_timestamp  = false;	// For backward compatibility
					ov::String _timestamp_mode_str;
					int _packet_silence_timeout_ms = 0;	 // Default value for packet silence timeout

				public:
					virtual ProviderType GetType() const = 0;
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxConnection, _max_connection)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTimestampMode, _timestamp_mode)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPacketSilenceTimeoutMs, _packet_silence_timeout_ms)
					void SetPacketSilenceTimeoutMs(int timeout_ms)
					{
						_packet_silence_timeout_ms = timeout_ms;
					}

				protected:
					void MakeList() override
					{
						Register<Optional>("MaxConnection", &_max_connection);
						Register<Optional>("TimestampMode", &_timestamp_mode_str, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   auto mode_str = _timestamp_mode_str.UpperCaseString();

											   if (mode_str == "ZEROBASED")
											   {
												   _timestamp_mode = TimestampMode::ZeroBased;
											   }
											   else if (mode_str == "ORIGINAL")
											   {
												   _timestamp_mode = TimestampMode::Original;
											   }
											   else
											   {
												   return CreateConfigErrorPtr("Invalid TimestampMode value: %s (expected: ZeroBased, Original)", _timestamp_mode_str.CStr());
											   }

											   return nullptr;
										   });

						// For backward compatibility
						Register<Optional>("UseIncomingTimestamp", &_use_incoming_timestamp, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   logw("Config", "UseIncomingTimestamp is deprecated. Please use TimestampMode instead.");

											   _timestamp_mode = _use_incoming_timestamp ? TimestampMode::Original : TimestampMode::Auto;

											   return nullptr;
										   });

						Register<Optional>("PacketSilenceTimeoutMs", &_packet_silence_timeout_ms, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   switch (GetType())
											   {
												   case ProviderType::Rtmp:
												   case ProviderType::Mpegts:
												   case ProviderType::WebRTC:
												   case ProviderType::Srt:
												   case ProviderType::Multiplex:
													   // Supported provider types
													   break;

												   default:
													   return CreateConfigErrorPtr("PacketSilenceTimeoutMs is not supported for this provider type: %s", StringFromProviderType(GetType()).CStr());
											   }
											   return nullptr;
										   });
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg