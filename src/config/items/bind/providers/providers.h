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
#include "./provider.h"
#include "./provider_with_options.h"
#include "./srt.h"

namespace cfg
{
	namespace bind
	{
		namespace pvd
		{
			struct Providers : public Item
			{
			protected:
				// PULL Providers (Client)
				Provider<cmn::SingularPort> _ovt{};
				Provider<cmn::SingularPort> _rtspc{};

				// PUSH Providers (Server)
				Provider<cmn::SingularPort> _rtmp{"1935/tcp"};
				Provider<cmn::SingularPort> _rtsp{"554/tcp"};
				Provider<cmn::RangedPort> _mpegts{"4000/udp"};

				SRT _srt{"9999/srt"};
				cmm::Webrtc _webrtc{"3333/tcp", "3334/tcp"};

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(GetOvt, _ovt);
				CFG_DECLARE_CONST_REF_GETTER_OF(GetRtmp, _rtmp)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetRtsp, _rtsp)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetRtspc, _rtspc)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetSrt, _srt)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetMpegts, _mpegts);
				CFG_DECLARE_CONST_REF_GETTER_OF(GetWebrtc, _webrtc)

			protected:
				void MakeList() override
				{
					Register<Optional>({"OVT", "ovt"}, &_ovt);
					Register<Optional>({"RTMP", "rtmp"}, &_rtmp);
					Register<Optional>({"RTSP", "rtsp"}, &_rtsp);
					Register<Optional>({"RTSPC", "rtspc"}, &_rtspc);
					Register<Optional>({"SRT", "srt"}, &_srt);
					Register<Optional>({"MPEGTS", "mpegts"}, &_mpegts);
					Register<Optional>({"WebRTC", "webrtc"}, &_webrtc);
				};
			};
		}  // namespace pvd
	}  // namespace bind
}  // namespace cfg
