//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "hls_id3v2.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct Event : public Item
				{
				protected:
					ov::String _trigger;
					HLSID3v2 _hls_id3v2;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTrigger, _trigger);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetHLSID3v2, _hls_id3v2);

				protected:
					void MakeList() override
					{
						Register("Trigger", &_trigger);
						Register<Optional>("HLSID3v2", &_hls_id3v2);
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
