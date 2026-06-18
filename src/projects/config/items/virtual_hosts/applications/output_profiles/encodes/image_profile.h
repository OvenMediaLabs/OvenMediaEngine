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
				struct ImageProfile : public Item
				{
				protected:
					ov::String _name;
					ov::String _codec;
					ov::String _modules;
					int _width		  = 0;
					int _height		  = 0;
					double _framerate = 0.0;
					BypassIfMatch _bypass_if_match;
					int _skip_frames = -1;
					ov::String _preset;
					int _qscale = 0;
					int _quality = 0;
					int _method = -1;
					bool _lossless = false;
					ov::String _chroma_sampling;
					int _speed = -1;
					int _crf = -1;
					bool _passthrough_av1 = false;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetCodec, _codec)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetModules, _modules);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetWidth, _width)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetHeight, _height)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFramerate, _framerate)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetBypassIfMatch, _bypass_if_match)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSkipFrames, _skip_frames)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPreset, _preset)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetQScale, _qscale)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetQuality, _quality)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMethod, _method)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLossless, _lossless)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetChromaSampling, _chroma_sampling)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSpeed, _speed)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetCrf, _crf)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPassthroughAV1, _passthrough_av1)

					void SetName(const ov::String &name)
					{
						_name = name;
					}

				protected:
					void MakeList() override
					{
						Register<Optional>("Name", &_name);
						Register<Optional>("Codec", &_codec);
						Register<Optional>("Modules", &_modules);
						Register<Optional>("Width", &_width);
						Register<Optional>("Height", &_height);
						Register<Optional>("Framerate", &_framerate);
						Register<Optional>("SkipFrames", &_skip_frames, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   if (_framerate > 0 && _skip_frames > 0)
											   {
												   logw("Config", "Use SkipFrames in the settings, the Framerate is ignored.");
											   }

											   return (_skip_frames >= -1 && _skip_frames <= 120) ? nullptr : CreateConfigErrorPtr("SkipFrames must be -1 or between 0 and 120");
										   });
						Register<Optional>("BypassIfMatch", &_bypass_if_match);
						// webp only; one of none/default/picture/photo/drawing/icon/text
						Register<Optional>("Preset", &_preset);
						// jpeg only; mjpeg qscale 1-31 (lower is better, 2 = encoder default)
						Register<Optional>("QScale", &_qscale);
						// webp only; libwebp quality 0-100 (higher is better, 75 = encoder default)
						Register<Optional>("Quality", &_quality);
						// webp only; libwebp method 0-6 (higher = smaller files, more CPU; 1 = OME default)
						Register<Optional>("Method", &_method);
						// webp only; lossless mode (Quality then controls compression effort)
						Register<Optional>("Lossless", &_lossless);
						// jpeg and avif; "420" (default) or "444"
						Register<Optional>("ChromaSampling", &_chroma_sampling);
						// avif only; libaom cpu-used 0-8 (lower = slower/better compression, 8 = OME default)
						Register<Optional>("Speed", &_speed);
						// avif only; libaom crf 0-63 (lower is better; OME defaults unset to 30)
						Register<Optional>("Crf", &_crf);
						// avif only; when set, AV1 input is rewrapped into AVIF at the source resolution without transcoding
						Register<Optional>("PassthroughAV1", &_passthrough_av1);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg