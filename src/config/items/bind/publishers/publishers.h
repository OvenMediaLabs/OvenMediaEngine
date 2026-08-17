//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../common/webrtc/webrtc.h"
#include "./publisher.h"
#include "./srt.h"

namespace cfg
{
	namespace bind
	{
		namespace pub
		{
			struct Publishers : public Item
			{
			protected:
				Publisher<cmn::SingularPort> _ovt{"9000/tcp"};
				Publisher<cmn::SingularPort> _llhls{"80/tcp", "443/tcp"};
				Publisher<cmn::SingularPort> _thumbnail{"80/tcp", "443/tcp"};
				Publisher<cmn::SingularPort> _hls{"80/tcp", "443/tcp"};

				cmm::Webrtc _webrtc{"3333/tcp", "3334/tcp"};

				SRT _srt{"9998/srt"};

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(GetOvt, _ovt)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetLLHls, _llhls)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetWebrtc, _webrtc)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetThumbnail, _thumbnail)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetHls, _hls)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetSrt, _srt)

			protected:
				void MakeList() override
				{
					Register<Optional>({"OVT", "ovt"}, &_ovt);
					Register<Optional>({"LLHLS", "llhls"}, &_llhls);
					Register<Optional>({"WebRTC", "webrtc"}, &_webrtc);
					Register<Optional>({"Thumbnail", "thumbnail"}, &_thumbnail);
					Register<Optional>({"HLS", "hls"}, &_hls);
					Register<Optional>({"SRT", "srt"}, &_srt);
				};
			};
		}  // namespace pub
	}  // namespace bind
}  // namespace cfg
