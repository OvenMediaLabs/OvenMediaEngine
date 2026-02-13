//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_type.h>

#include "rendition_template_types.h"

// <AduioTemplate>
// 	<VariantName>bypass_audio</VariantName>
//  <BypassedTrack>true</BypassedTrack>
// 	<MaxBitrate>128000</MaxBitrate>
// 	<MinBitrate>128000</MinBitrate>
// 	<MaxSamplerate>48000</MaxSamplerate>
// 	<MinSamplerate>48000</MinSamplerate>
// 	<MaxChannel>2</MaxChannel>
// 	<MinChannel>2</MinChannel>
// </AduioTemplate>

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct AudioTemplate : public Item
				{
				protected:
					ov::String _variant_name;
					ov::String _encoding_type		 = "all";  // all, encoded, bypassed
					EncodingType _encoding_type_enum = EncodingType::All;
					int _index_hint					 = -1;
					int _max_bitrate				 = -1;
					int _min_bitrate				 = -1;
					int _max_samplerate				 = -1;
					int _min_samplerate				 = -1;
					int _max_channel				 = -1;
					int _min_channel				 = -1;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetVariantName, _variant_name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetEncodingType, _encoding_type_enum);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetIndexHint, _index_hint);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxBitrate, _max_bitrate);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMinBitrate, _min_bitrate);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxSamplerate, _max_samplerate);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMinSamplerate, _min_samplerate);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxChannel, _max_channel);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMinChannel, _min_channel);

					bool IsMatched(const std::shared_ptr<MediaTrack> &track) const
					{
						if (track->GetMediaType() != ::cmn::MediaType::Audio)
						{
							return false;
						}

						if (_variant_name.IsEmpty() == false && _variant_name != track->GetVariantName())
						{
							return false;
						}

						if (_encoding_type_enum == EncodingType::Encoded && track->IsBypass() == true)
						{
							return false;
						}

						if (_encoding_type_enum == EncodingType::Bypassed && track->IsBypass() == false)
						{
							return false;
						}

						if (_index_hint >= 0 && track->GetGroupIndex() != _index_hint)
						{
							return false;
						}

						if (_max_bitrate > 0 && track->GetBitrate() > _max_bitrate)
						{
							return false;
						}

						if (_min_bitrate > 0 && track->GetBitrate() < _min_bitrate)
						{
							return false;
						}

						if (_max_samplerate > 0 && track->GetSampleRate() > _max_samplerate)
						{
							return false;
						}

						if (_min_samplerate > 0 && track->GetSampleRate() < _min_samplerate)
						{
							return false;
						}

						if (_max_channel > 0 && track->GetChannel().GetCounts() > static_cast<uint32_t>(_max_channel))
						{
							return false;
						}

						if (_min_channel > 0 && track->GetChannel().GetCounts() < static_cast<uint32_t>(_min_channel))
						{
							return false;
						}

						return true;
					}

				protected:
					void MakeList() override
					{
						Register<Optional>({"VariantName", "variant_name"}, &_variant_name);
						Register<Optional>({"EncodingType", "encoding_type"}, &_encoding_type, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   if (_encoding_type != "all" && _encoding_type != "encoded" && _encoding_type != "bypassed")
											   {
												   return CreateConfigErrorPtr("Invalid EncodingType: %s (all, encoded, bypassed)", _encoding_type.CStr());
											   }

											   if (_encoding_type.LowerCaseString() == "encoded")
											   {
												   _encoding_type_enum = EncodingType::Encoded;
											   }
											   else if (_encoding_type.LowerCaseString() == "bypassed")
											   {
												   _encoding_type_enum = EncodingType::Bypassed;
											   }

											   return nullptr;
										   });
						Register<Optional>({"MaxBitrate", "max_bitrate"}, &_max_bitrate);
						Register<Optional>({"MinBitrate", "min_bitrate"}, &_min_bitrate);
						Register<Optional>({"MaxSamplerate", "max_samplerate"}, &_max_samplerate);
						Register<Optional>({"MinSamplerate", "min_samplerate"}, &_min_samplerate);
						Register<Optional>({"MaxChannel", "max_channel"}, &_max_channel);
						Register<Optional>({"MinChannel", "min_channel"}, &_min_channel);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg