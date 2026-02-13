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
					int _skip_frames = 0;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetCodec, _codec)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetModules, _modules);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetWidth, _width)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetHeight, _height)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFramerate, _framerate)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetBypassIfMatch, _bypass_if_match)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSkipFrames, _skip_frames)

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

											   return (_skip_frames >= 0 && _skip_frames <= 120) ? nullptr : CreateConfigErrorPtr("SkipFrames must be between 0 and 120");
										   });
						Register<Optional>("BypassIfMatch", &_bypass_if_match);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg