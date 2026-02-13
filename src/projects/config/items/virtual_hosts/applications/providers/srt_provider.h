//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

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
				struct SrtProvider : public Provider
				{
				protected:
					// true: block(disconnect) new incoming stream
					// false: don't block new incoming stream
					bool _is_block_duplicate_stream_name = true;
					AudioMap _audio_map;

				public:
					ProviderType GetType() const override
					{
						return ProviderType::Srt;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(IsBlockDuplicateStreamName, _is_block_duplicate_stream_name)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioMap, _audio_map)

				protected:
					void MakeList() override
					{
						Provider::MakeList();

						Register<Optional>("BlockDuplicateStreamName", &_is_block_duplicate_stream_name);
						Register<Optional>("AudioMap", &_audio_map);
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg