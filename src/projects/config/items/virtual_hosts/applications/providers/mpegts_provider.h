//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "mpegts/stream_map/stream_map.h"
#include "provider.h"
#include "track_map/audio_map.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct MpegtsProvider : public Provider
				{
				protected:
					mpegts::StreamMap _stream_map;
					AudioMap _audio_map;

				public:
					ProviderType GetType() const override
					{
						return ProviderType::Mpegts;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamMap, _stream_map)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioMap, _audio_map)

				protected:
					void MakeList() override
					{
						Provider::MakeList();

						Register<Optional>({"StreamMap", "streams"}, &_stream_map);
						Register<Optional>("AudioMap", &_audio_map);
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg