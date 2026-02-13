//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "hls_options/drm.h"
#include "hls_publisher.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct LLHlsPublisher : public HlsPublisher
				{
				protected:
					double _chunk_duration	  = 0.5;
					double _part_hold_back	  = 0;	// it will be set to 3 * chunk_duration automatically
					bool _enable_preload_hint = true;
					Drm _drm;

				public:
					PublisherType GetType() const override
					{
						return PublisherType::LLHls;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetChunkDuration, _chunk_duration)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPartHoldBack, _part_hold_back)
					CFG_DECLARE_CONST_REF_GETTER_OF(IsPreloadHintEnabled, _enable_preload_hint)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDrm, _drm)

				protected:
					void MakeList() override
					{
						HlsPublisher::MakeList();

						Register<Optional>("ChunkDuration", &_chunk_duration);
						Register<Optional>("PartHoldBack", &_part_hold_back);
						Register<Optional>("EnablePreloadHint", &_enable_preload_hint);
						Register<Optional>("DRM", &_drm);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
