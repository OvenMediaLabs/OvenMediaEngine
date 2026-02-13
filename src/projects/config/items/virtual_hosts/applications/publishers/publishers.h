//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "file_publisher.h"
#include "hls_publisher.h"
#include "ll_hls_publisher.h"
#include "mpegtspush_publisher.h"
#include "ovt_publisher.h"
#include "push_publisher.h"
#include "rtmppush_publisher.h"
#include "srt_publisher.h"
#include "srtpush_publisher.h"
#include "thumbnail_publisher.h"
#include "webrtc_publisher.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct Publishers : public Item
				{
					std::vector<const Publisher *> GetPublisherList() const
					{
						return {
							// &_mpegtspush_publisher,
							&_webrtc_publisher,
							&_ll_hls_publisher,
							&_ovt_publisher,
							&_file_publisher,
							// &_rtmppush_publisher,
							&_thumbnail_publisher,
							// &_srtpush_publisher,
							&_hls_publisher,
							&_push_publisher,
							&_srt_publisher,
						};
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetAppWorkerCount, _app_worker_count)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamWorkerCount, _stream_worker_count)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDelayBufferTimeMs, _delay_buffer_time_ms)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetWebrtcPublisher, _webrtc_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLLHlsPublisher, _ll_hls_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOvtPublisher, _ovt_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFilePublisher, _file_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetThumbnailPublisher, _thumbnail_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetHlsPublisher, _hls_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPushPublisher, _push_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSrtPublisher, _srt_publisher)

					// Deprecated
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRtmpPushPublisher, _rtmppush_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMpegtsPushPublisher, _mpegtspush_publisher)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSrtPushPublisher, _srtpush_publisher)

				protected:
					void MakeList() override
					{
						Register<Optional>("AppWorkerCount", &_app_worker_count);
						Register<Optional>("StreamWorkerCount", &_stream_worker_count);
						Register<Optional>("DelayBufferTimeMs", &_delay_buffer_time_ms);
						Register<Optional>({"WebRTC", "webrtc"}, &_webrtc_publisher);
						Register<Optional>({"LLHLS", "llhls"}, &_ll_hls_publisher);
						Register<Optional>({"HLS", "hls"}, &_hls_publisher);
						Register<Optional>({"OVT", "ovt"}, &_ovt_publisher);
						Register<Optional>({"FILE", "file"}, &_file_publisher);
						Register<Optional>({"Thumbnail", "thumbnail"}, &_thumbnail_publisher);
						Register<Optional>({"Push", "push"}, &_push_publisher);
						Register<Optional>({"SRT", "srt"}, &_srt_publisher);

						// Deprecated
						Register<Optional>("MPEGTSPush", &_mpegtspush_publisher, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   _push_publisher.SetParsed(true);
											   logw("Config", "MPEGTSPush will be deprecated. Please use Push instead");
											   return nullptr;
										   });
						Register<Optional>({"RTMPPush", "rtmpPush"}, &_rtmppush_publisher, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   _push_publisher.SetParsed(true);
											   logw("Config", "RTMPPush will be deprecated. Please use Push instead");
											   return nullptr;
										   });
						Register<Optional>({"SRTPush", "srtPush"}, &_srtpush_publisher, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   _push_publisher.SetParsed(true);
											   logw("Config", "SRTPush will be deprecated. Please use Push instead");
											   return nullptr;
										   });
					}

					int _app_worker_count	  = 1;
					int _stream_worker_count  = 8;
					int _delay_buffer_time_ms = 0;

					MpegtsPushPublisher _mpegtspush_publisher;
					WebrtcPublisher _webrtc_publisher;
					LLHlsPublisher _ll_hls_publisher;
					OvtPublisher _ovt_publisher;
					FilePublisher _file_publisher;
					ThumbnailPublisher _thumbnail_publisher;
					PushPublisher _push_publisher;
					SrtPublisher _srt_publisher;

					// Deprecated
					RtmpPushPublisher _rtmppush_publisher;
					SrtPushPublisher _srtpush_publisher;
					HlsPublisher _hls_publisher;
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
