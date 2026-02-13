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

// <VideoTemplate>
// 	<!-- If you receive 3 Video Tracks from WebRTC Simulcast,
// 		3 video tracks with the same name bypass_video are created. -->
// 	<VariantName>bypass_video</VariantName>
// 	<BypassedTrack>true</BypassedTrack>
// 	<MaxWidth>1080</MaxWidth>
// 	<MinWidth>240</MinWidth>
// 	<MaxHeight>720<MaxHeight>
// 	<MinHeight>240</MinHeight>
// 	<MaxFPS>30</MaxFPS>
// 	<MinFPS>30</MinFPS>
// 	<MaxBitrate>2000000</MaxBitrate>
// 	<MinBitrate>500000</MinBitrate>
// </VideoTemplate>

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct VideoTemplate : public Item
				{
				protected:
					ov::String _variant_name;
					ov::String _encoding_type		 = "all";  // all, encoded, bypassed
					EncodingType _encoding_type_enum = EncodingType::All;
					int _index_hint					 = -1;
					int _max_width					 = -1;
					int _min_width					 = -1;
					int _max_height					 = -1;
					int _min_height					 = -1;
					int _max_framerate				 = -1;
					int _min_framerate				 = -1;
					int _max_bitrate				 = -1;
					int _min_bitrate				 = -1;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetVariantName, _variant_name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetEncodingType, _encoding_type_enum);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetIndexHint, _index_hint);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxWidth, _max_width);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMinWidth, _min_width);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxHeight, _max_height);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMinHeight, _min_height);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxFPS, _max_framerate);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMinFPS, _min_framerate);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxBitrate, _max_bitrate);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMinBitrate, _min_bitrate);

					bool IsMatched(const std::shared_ptr<MediaTrack> &track) const
					{
						if (track->GetMediaType() != ::cmn::MediaType::Video)
						{
							return false;
							;
						}

						if (_variant_name.IsEmpty() == false && track->GetVariantName() != _variant_name)
						{
							return false;
						}

						if (_encoding_type_enum == EncodingType::Bypassed && track->IsBypass() == false)
						{
							return false;
						}

						if (_encoding_type_enum == EncodingType::Encoded && track->IsBypass() == true)
						{
							return false;
						}

						if (_index_hint >= 0 && track->GetGroupIndex() != _index_hint)
						{
							return false;
						}

						if (_max_width > 0 && track->GetWidth() > _max_width)
						{
							return false;
						}

						if (_min_width > 0 && track->GetWidth() < _min_width)
						{
							return false;
						}

						if (_max_height > 0 && track->GetHeight() > _max_height)
						{
							return false;
						}

						if (_min_height > 0 && track->GetHeight() < _min_height)
						{
							return false;
						}

						if (_max_framerate > 0 && track->GetFrameRate() > _max_framerate)
						{
							return false;
						}

						if (_min_framerate > 0 && track->GetFrameRate() < _min_framerate)
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
						Register<Optional>({"MaxWidth", "max_width"}, &_max_width);
						Register<Optional>({"MinWidth", "min_width"}, &_min_width);
						Register<Optional>({"MaxHeight", "max_height"}, &_max_height);
						Register<Optional>({"MinHeight", "min_height"}, &_min_height);
						Register<Optional>({"MaxFramerate", "max_framerate"}, &_max_framerate);
						Register<Optional>({"MinFramerate", "min_framerate"}, &_min_framerate);
						Register<Optional>({"MaxBitrate", "max_bitrate"}, &_max_bitrate);
						Register<Optional>({"MinBitrate", "min_bitrate"}, &_min_bitrate);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg