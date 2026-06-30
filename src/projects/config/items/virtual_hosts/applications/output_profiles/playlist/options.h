//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct Options : public Item
				{
				protected:
					bool _webrtc_auto_abr		  = true;

					// Expose audio-only renditions inside video playlists that share their audio
					// codec, so a video WebRTC session can switch to audio-only on the same
					// PeerConnection. Off by default: it changes the playlist advertised to a
					// video session that also declares an audio-only rendition.
					bool _webrtc_audio_only_fallback = false;

					// If this option is true, ts publisher will use this playlist
					bool _enable_ts_packaging	  = false;

					// -1(default) : absolute - /app/stream/chunklist.m3u8
					// 0 : relative file - chunklist.m3u8
					// 1 : relative stream - ../stream/chunklist.m3u8
					// 2 : relative app - ../../app/stream/chunklist.m3u8
					int _hls_chunklist_path_depth = -1;
					bool _enable_subtitles		  = true;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsWebRtcAutoAbr, _webrtc_auto_abr);
					CFG_DECLARE_CONST_REF_GETTER_OF(IsWebRtcAudioOnlyFallback, _webrtc_audio_only_fallback);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetHlsChunklistPathDepth, _hls_chunklist_path_depth);
					CFG_DECLARE_CONST_REF_GETTER_OF(IsTsPackagingEnabled, _enable_ts_packaging);
					CFG_DECLARE_CONST_REF_GETTER_OF(IsSubtitlesEnabled, _enable_subtitles);

				protected:
					void MakeList() override
					{
						Register<Optional>("WebRtcAutoAbr", &_webrtc_auto_abr);
						Register<Optional>("WebRtcAudioOnlyFallback", &_webrtc_audio_only_fallback);
						Register<Optional>({"HLSChunklistPathDepth", "hlsChunklistPathDepth"}, &_hls_chunklist_path_depth);
						Register<Optional>("EnableTsPackaging", &_enable_ts_packaging);
						Register<Optional>("EnableSubtitles", &_enable_subtitles);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg