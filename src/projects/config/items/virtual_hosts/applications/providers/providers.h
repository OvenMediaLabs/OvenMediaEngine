//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "file_provider.h"
#include "mpegts_provider.h"
#include "multiplex_provider.h"
#include "ovt_provider.h"
#include "rtmp_provider.h"
#include "rtsp_provider.h"
#include "rtsp_pull_provider.h"
#include "scheduled_provider.h"
#include "srt_provider.h"
#include "webrtc_provider.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct Providers : public Item
				{
					std::vector<const Provider *> GetProviderList() const
					{
						return {
							&_rtmp_provider,
							&_rtsp_pull_provider,
							&_rtsp_provider,
							&_ovt_provider,
							&_srt_provider,
							&_mpegts_provider,
							&_webrtc_provider,
							&_scheduled_provider,
							&_multiplex_provider};
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetRtmpProvider, _rtmp_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRtspPullProvider, _rtsp_pull_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRtspProvider, _rtsp_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOvtProvider, _ovt_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSrtProvider, _srt_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMpegtsProvider, _mpegts_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetWebrtcProvider, _webrtc_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFileProvider, _file_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetScheduledProvider, _scheduled_provider)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMultiplexProvider, _multiplex_provider)

				protected:
					void MakeList() override
					{
						Register<Optional>({"RTMP", "rtmp"}, &_rtmp_provider);
						Register<Optional>({"RTSPPull", "rtspPull"}, &_rtsp_pull_provider);
						Register<Optional>({"RTSP", "rtsp"}, &_rtsp_provider);
						Register<Optional>({"OVT", "ovt"}, &_ovt_provider);
						Register<Optional>({"SRT", "srt"}, &_srt_provider);
						Register<Optional>({"WebRTC", "webrtc"}, &_webrtc_provider);
						Register<Optional>({"FILE", "file"}, &_file_provider);
						Register<Optional>({"Schedule", "schedule"}, &_scheduled_provider);
						Register<Optional>({"Multiplex", "multiplex"}, &_multiplex_provider);
						Register<Optional>({"MPEGTS", "mpegts"}, &_mpegts_provider, nullptr,
										   [this]() -> std::shared_ptr<ConfigError> {
											   // MPEGTS provider's default value of PacketSilenceTimeoutMs is 1500 ms since it is udp based protocol.
											   if (_mpegts_provider.GetPacketSilenceTimeoutMs() == 0)
											   {
												   logi("Config", "Setting default PacketSilenceTimeoutMs to 1500 ms for MPEGTS provider.");
												   _mpegts_provider.SetPacketSilenceTimeoutMs(1500);
											   }

											   return nullptr;
										   });
					};

					RtmpProvider _rtmp_provider;
					RtspPullProvider _rtsp_pull_provider;
					RtspProvider _rtsp_provider;
					OvtProvider _ovt_provider;
					SrtProvider _srt_provider;
					MpegtsProvider _mpegts_provider;
					WebrtcProvider _webrtc_provider;
					FileProvider _file_provider;
					ScheduledProvider _scheduled_provider;
					MultiplexProvider _multiplex_provider;
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg