//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace alrt
	{
		namespace rule
		{
			struct Ingress : public Item
			{
			protected:
				bool _stream_status	 = false;
				int32_t _min_bitrate = 0;
				ov::String _min_bitrate_string;
				int32_t _max_bitrate = 0;
				ov::String _max_bitrate_string;
				double _min_framerate		  = 0.0;
				double _max_framerate		  = 0.0;
				int32_t _min_width			  = 0;
				int32_t _max_width			  = 0;
				int32_t _min_height			  = 0;
				int32_t _max_height			  = 0;
				int32_t _min_samplerate		  = 0;
				int32_t _max_samplerate		  = 0;
				bool _long_key_frame_interval = false;
				bool _has_b_frames			  = false;

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(IsStreamStatus, _stream_status)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMinBitrate, _min_bitrate)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMinBitrateString, _min_bitrate_string)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxBitrate, _max_bitrate)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxBitrateString, _max_bitrate_string)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMinFramerate, _min_framerate)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxFramerate, _max_framerate)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMinWidth, _min_width)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxWidth, _max_width)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMinHeight, _min_height)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxHeight, _max_height)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMinSamplerate, _min_samplerate)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxSamplerate, _max_samplerate)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsLongKeyFrameInterval, _long_key_frame_interval)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetHasBFrames, _has_b_frames)

			protected:
				void MakeList() override
				{
					Register<Optional>("StreamStatus", &_stream_status, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						_stream_status = true;
						return nullptr;
					});
					Register<Optional>("MinBitrate", &_min_bitrate_string, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						auto min_bitrate_string = _min_bitrate_string.UpperCaseString();
						int multiplier			= 1;
						if (min_bitrate_string.HasSuffix("K"))
						{
							multiplier = 1024;
						}
						else if (min_bitrate_string.HasSuffix("M"))
						{
							multiplier = 1024 * 1024;
						}

						_min_bitrate = static_cast<int32_t>(ov::Converter::ToFloat(min_bitrate_string) * multiplier);

						return (_min_bitrate > 0) ? nullptr : CreateConfigErrorPtr("MinBitrate must be greater than 0");
					});
					Register<Optional>("MaxBitrate", &_max_bitrate_string, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						auto max_bitrate_string = _max_bitrate_string.UpperCaseString();
						int multiplier			= 1;
						if (max_bitrate_string.HasSuffix("K"))
						{
							multiplier = 1024;
						}
						else if (max_bitrate_string.HasSuffix("M"))
						{
							multiplier = 1024 * 1024;
						}

						_max_bitrate = static_cast<int32_t>(ov::Converter::ToFloat(max_bitrate_string) * multiplier);

						return (_max_bitrate > 0) ? nullptr : CreateConfigErrorPtr("MaxBitrate must be greater than 0");
					});
					Register<Optional>("MinFramerate", &_min_framerate);
					Register<Optional>("MaxFramerate", &_max_framerate);
					Register<Optional>("MinWidth", &_min_width);
					Register<Optional>("MaxWidth", &_max_width);
					Register<Optional>("MinHeight", &_min_height);
					Register<Optional>("MaxHeight", &_max_height);
					Register<Optional>("MinSamplerate", &_min_samplerate);
					Register<Optional>("MaxSamplerate", &_max_samplerate);
					Register<Optional>("LongKeyFrameInterval", &_long_key_frame_interval, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						_long_key_frame_interval = true;
						return nullptr;
					});
					Register<Optional>("HasBFrames", &_has_b_frames, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						_has_b_frames = true;
						return nullptr;
					});
				}
			};
		}  // namespace rule
	}  // namespace alrt
}  // namespace cfg
