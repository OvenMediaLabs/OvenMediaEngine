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
					int _packet_silence_timeout_ms					= 0;  // Default value for packet silence timeout
					// Whether `_packet_silence_timeout_ms` currently holds the value the operator asked
					// for, rather than a provider default. Set while parsing `PacketSilenceTimeoutMs`
					// and cleared again by `SetDefaultPacketSilenceTimeoutMs()`.
					bool _is_packet_silence_timeout_ms_configured	= false;
					// How long an input may stay silent between asking to publish and its first media packet.
					// Off unless the operator sets it: a source that needs a longer wait is rare enough
					// that widening the window for everyone would change behaviour nobody asked to change.
					// A source at a low frame rate can legitimately send nothing for a long time:
					// a captured OBS session at 1 fps sent nothing for 23.5 s after its publish command,
					// and only then its first media packet.
					int _first_media_wait_timeout_ms				= 0;
					// Whether the value in `_first_media_wait_timeout_ms` came from the operator
					bool _is_first_media_wait_timeout_ms_configured = false;

				public:
					virtual ProviderType GetType() const = 0;
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxConnection, _max_connection)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTimestampMode, _timestamp_mode)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPacketSilenceTimeoutMs, _packet_silence_timeout_ms)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFirstMediaWaitTimeoutMs, _first_media_wait_timeout_ms)
					bool IsFirstMediaWaitTimeoutMsConfigured() const
					{
						return _is_first_media_wait_timeout_ms_configured;
					}
					bool IsPacketSilenceTimeoutMsConfigured() const
					{
						return _is_packet_silence_timeout_ms_configured;
					}
					// Overrides the value with a provider default. The value is no longer the operator's
					// after this, so anything that must honor only an explicit setting stops seeing it.
					void SetDefaultPacketSilenceTimeoutMs(int timeout_ms)
					{
						_packet_silence_timeout_ms				 = timeout_ms;
						_is_packet_silence_timeout_ms_configured = false;
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
											   // A negative value would defeat both guards in the channel task runner:
											   // it is not `0`, so the timeout counts as active, and every elapsed value
											   // exceeds it - including the `-1` that means no data has arrived yet.
											   if (_packet_silence_timeout_ms < 0)
											   {
												   return CreateConfigErrorPtr("PacketSilenceTimeoutMs must not be negative: %d", _packet_silence_timeout_ms);
											   }

											   // This callback only runs when the option is present in the config
											   _is_packet_silence_timeout_ms_configured = true;

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

						Register<Optional>("FirstMediaWaitTimeoutMs", &_first_media_wait_timeout_ms, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   // This callback only runs when the option is present in the config
											   _is_first_media_wait_timeout_ms_configured = true;

											   // A `0` is rejected rather than silently ignored:
											   // it would leave this wait with no timeout at all,
											   // and an empty or non-numeric element also yields `0`.
											   if (_first_media_wait_timeout_ms <= 0)
											   {
												   return CreateConfigErrorPtr("FirstMediaWaitTimeoutMs requires a positive value: %d", _first_media_wait_timeout_ms);
											   }

											   switch (GetType())
											   {
												   case ProviderType::Rtmp:
													   // Only RTMP waits for a first packet it cannot publish without:
													   // the codec sequence headers arrive with it.
													   break;

												   default:
													   return CreateConfigErrorPtr("FirstMediaWaitTimeoutMs is not supported for this provider type: %s", StringFromProviderType(GetType()).CStr());
											   }
											   return nullptr;
										   });
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg