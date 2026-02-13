//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "decode_video.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace dec
			{
				struct Decodes : public Item
				{
				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetVideo, _video);

					// Informal Option
					CFG_DECLARE_CONST_REF_GETTER_OF(GetH264hasBFrames, _h264_has_bframes);

				protected:
					void MakeList() override
					{
						Register<Optional>("Video", &_video);
						Register<Optional>("H264hasBframes", &_h264_has_bframes);
					}

					int32_t _h264_has_bframes = 16;
					dec::DecodeVideo _video;
				};
			}  // namespace dec
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg