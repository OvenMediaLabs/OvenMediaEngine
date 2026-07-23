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
			namespace pvd
			{
				struct HLSID3v2 : public Item
				{
				protected:
					ov::String _event_type;	 // video, audio, both
					ov::String _frame_type;	 // TXXX, T???, PRIV
					ov::String _info;
					ov::String _data;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetEventType, _event_type);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFrameType, _frame_type);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetInfo, _info);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetData, _data);

				protected:
					void MakeList() override
					{
						Register("EventType", &_event_type);
						Register("FrameType", &_frame_type);
						Register<Optional>("Info", &_info);
						Register("Data", &_data);
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
