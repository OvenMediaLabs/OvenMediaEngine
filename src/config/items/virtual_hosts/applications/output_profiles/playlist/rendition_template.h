//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "audio_template.h"
#include "video_template.h"

/*
<RenditionTemplate>
	<Name>${Height}p</Name> <!-- {Bitrates}, {Width}, {FPS}, {VideoPublicName} -->
	<VideoTemplate>
		<!-- If you receive 3 Video Tracks from WebRTC Simulcast, 
			3 video tracks with the same name bypass_video are created. -->
		<VariantName>bypass_video</VariantName>
		<BypassedTrack>true</BypassedTrack>
		<MaxWidth>1080</MaxWidth>
		<MinWidth>240</MinWidth>
		<MaxHeight>720<MaxHeight>
		<MinHeight>240</MinHeight>
		<MaxFPS>30</MaxFPS>
		<MinFPS>30</MinFPS>
		<MaxBitrate>2000000</MaxBitrate>
		<MinBitrate>500000</MinBitrate>
	</VideoTemplate>
	<AduioTemplate>
		<VariantName>bypass_audio</VariantName>
		<MaxBitrate>128000</MaxBitrate>
		<MinBitrate>128000</MinBitrate>
		<MaxSamplerate>48000</MaxSamplerate>
		<MinSamplerate>48000</MinSamplerate>
		<MaxChannel>2</MaxChannel>
		<MinChannel>2</MinChannel>
	</AduioTemplate>
</RenditionTemplate>
*/

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct RenditionTemplate : public Item
				{
				protected:
					ov::String _name;
					AudioTemplate _audio_template;
					VideoTemplate _video_template;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioTemplate, _audio_template);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetVideoTemplate, _video_template);

				protected:
					void MakeList() override
					{
						Register({"Name", "name"}, &_name);
						Register<Optional>({"VideoTemplate", "video_template"}, &_video_template);
						Register<Optional>({"AudioTemplate", "audio_template"}, &_audio_template);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg