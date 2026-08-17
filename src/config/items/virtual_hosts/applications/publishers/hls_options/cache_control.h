//=============================================================================
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
			namespace pub
			{
				struct CacheControl : public Item
				{
				protected:
					// 0 means no-cache no-store
					int _master_playlist_max_age		   = 0;
					int _chunklist_max_age				   = 0;
					int _chunklist_with_directives_max_age = 60;
					int _segment_max_age				   = -1;
					int _partial_segment_max_age		   = -1;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMasterPlaylistMaxAge, _master_playlist_max_age)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetChunklistMaxAge, _chunklist_max_age)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetChunklistWithDirectivesMaxAge, _chunklist_with_directives_max_age)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSegmentMaxAge, _segment_max_age)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPartialSegmentMaxAge, _partial_segment_max_age)

				protected:
					void MakeList() override
					{
						Register<Optional>("MasterPlaylistMaxAge", &_master_playlist_max_age);
						Register<Optional>("ChunklistMaxAge", &_chunklist_max_age);
						Register<Optional>("ChunklistWithDirectivesMaxAge", &_chunklist_with_directives_max_age);
						Register<Optional>("SegmentMaxAge", &_segment_max_age);
						Register<Optional>("PartialSegmentMaxAge", &_partial_segment_max_age);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
