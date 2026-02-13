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
				struct Playlists : public Item
				{
				protected:
					std::vector<ov::String> _playlists;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPlaylists, _playlists);

				protected:
					void MakeList() override
					{
						Register("Playlist", &_playlists);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
