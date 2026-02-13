//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "bypass_if_match.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct VideoProfile : public Item
				{
				protected:
					ov::String _name;
					bool _bypass = false;
					ov::String _codec;
					ov::String _modules;

					int _width	 = 0;
					int _height	 = 0;
					int _bitrate = 0;
					ov::String _bitrate_string;
					double _framerate = 0.0;
					ov::String _preset;
					int _thread_count					= -1;
					int _key_frame_interval				= 0;
					ov::String _key_frame_interval_type = "frame";
					int _b_frames						= 0;
					BypassIfMatch _bypass_if_match;
					ov::String _profile;
					int _lookahead	 = -1;

					// SkipFrames
					// If the set value is greater than or equal to 0, the skip frame is automatically calculated.
					// The skip frame is not less than the value set by the user.
					// -1 : No SkipFrame
					// 0 ~ 120 : minimum value of SkipFrames. it is automatically calculated and the SkipFrames value is changed.
					int _skip_frames = -1;

					// User-defined encoder options passed directly to the underlying encoder without validation.
					// example: "nal-hrd=cbr:force-cfr=1"
					ov::String _extra_options = "";

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name)
					CFG_DECLARE_CONST_REF_GETTER_OF(IsBypass, _bypass)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetModules, _modules);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetCodec, _codec)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetWidth, _width)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetHeight, _height)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetBitrate, _bitrate)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetBitrateString, _bitrate_string)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFramerate, _framerate)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPreset, _preset)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetThreadCount, _thread_count)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetKeyFrameInterval, _key_frame_interval)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetKeyFrameIntervalType, _key_frame_interval_type)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetBFrames, _b_frames)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetBypassIfMatch, _bypass_if_match)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetProfile, _profile)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSkipFrames, _skip_frames)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLookahead, _lookahead)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetExtraOptions, _extra_options)

					void SetName(const ov::String &name)
					{
						_name = name;
					}
					void SetBypass(bool bypass)
					{
						_bypass = bypass;
					}
					void SetCodec(const ov::String &codec)
					{
						_codec = codec;
					}
					void SetWidth(int width)
					{
						_width = width;
					}
					void SetHeight(int height)
					{
						_height = height;
					}
					void SetBitrate(int bitrate)
					{
						_bitrate = bitrate;
					}
					void SetBitrateString(const ov::String &bitrate_string)
					{
						_bitrate_string = bitrate_string;
					}
					void SetFramerate(double framerate)
					{
						_framerate = framerate;
					}
					void SetPreset(const ov::String &preset)
					{
						_preset = preset;
					}
					void SetThreadCount(int thread_count)
					{
						_thread_count = thread_count;
					}
					void SetKeyFrameInterval(int key_frame_interval)
					{
						_key_frame_interval = key_frame_interval;
					}
					void SetBFrames(int b_frames)
					{
						_b_frames = b_frames;
					}
					void SetLookahead(int lookahead)
					{
						_lookahead = lookahead;
					}
					void SetExtraOptions(const ov::String &extra_options)
					{
						_extra_options = extra_options;
					}

				protected:
					void MakeList() override
					{
						Register<Optional>("Name", &_name);
						Register<Optional>("Bypass", &_bypass);
						Register<Optional>("Codec", &_codec,
										   [=]() -> std::shared_ptr<ConfigError> {
											   // <Codec> is an option when _bypass is true
											   return (_bypass) ? nullptr : CreateConfigErrorPtr("Codec must be specified when bypass is false");
										   });
						Register<Optional>("Modules", &_modules);
						Register<Optional>("Bitrate", &_bitrate_string, [=]() -> std::shared_ptr<ConfigError> {
								// <Bitrate> is an option when _bypass is true
								return (_bypass) ? nullptr : CreateConfigErrorPtr("Bitrate must be specified when bypass is false"); }, [=]() -> std::shared_ptr<ConfigError> {
								auto bitrate_string = _bitrate_string.UpperCaseString();
								int multiplier = 1;
								if (bitrate_string.HasSuffix("K"))
								{
									multiplier = 1024;
								}
								else if (bitrate_string.HasSuffix("M"))
								{
									multiplier = 1024 * 1024;
								}

								_bitrate = static_cast<int>(ov::Converter::ToFloat(bitrate_string) * multiplier);

								return (_bitrate > 0) ? nullptr : CreateConfigErrorPtr("Bitrate must be greater than 0"); });
						Register<Optional>("Width", &_width);
						Register<Optional>("Height", &_height);
						Register<Optional>("Framerate", &_framerate);
						Register<Optional>("SkipFrames", &_skip_frames, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   if (_framerate > 0 && _skip_frames > 0)
											   {
												   logw("Config", "Use SkipFrames in the settings, the Framerate is ignored.");
											   }

											   return (_skip_frames >= 0 && _skip_frames <= 120) ? nullptr : CreateConfigErrorPtr("SkipFrames must be between 0 and 120");
										   });
						Register<Optional>("Preset", &_preset, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   auto preset = _preset.LowerCaseString();
											   if (preset == "slower" || preset == "slow" || preset == "medium" || preset == "fast" || preset == "faster")
											   {
												   return nullptr;
											   }
											   return CreateConfigErrorPtr("Preset must be slower, slow, medium, fast, or faster");
										   });
						Register<Optional>("ThreadCount", &_thread_count);
						Register<Optional>("KeyFrameIntervalType", &_key_frame_interval_type, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   auto key_frame_interval_type = _key_frame_interval_type.LowerCaseString();
											   if (key_frame_interval_type == "frame" || key_frame_interval_type == "time")
											   {
												   return nullptr;
											   }

											   return CreateConfigErrorPtr("KeyFrameIntervalType must be frame or time");
										   });
						Register<Optional>("KeyFrameInterval", &_key_frame_interval, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   if (_key_frame_interval_type == "time")
											   {
												   return (_key_frame_interval > 0 && _key_frame_interval <= 10000) ? nullptr : CreateConfigErrorPtr("KeyFrameInterval must be between 0 and 10000");
											   }

											   return (_key_frame_interval >= 0 && _key_frame_interval <= 600) ? nullptr : CreateConfigErrorPtr("KeyFrameInterval must be between 0 and 600");
										   });

						Register<Optional>("BFrames", &_b_frames, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   return (_b_frames >= 0 && _b_frames <= 16) ? nullptr : CreateConfigErrorPtr("BFrames must be between 0 and 16");
										   });
						Register<Optional>("BypassIfMatch", &_bypass_if_match);
						Register<Optional>("Profile", &_profile, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   auto profile = _profile.LowerCaseString();
											   if (profile == "baseline" || profile == "main" || profile == "high")
											   {
												   return nullptr;
											   }
											   return CreateConfigErrorPtr("Profile must be baseline, main or high");
										   });
						Register<Optional>("Lookahead", &_lookahead);
						Register<Optional>("ExtraOptions", &_extra_options);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg