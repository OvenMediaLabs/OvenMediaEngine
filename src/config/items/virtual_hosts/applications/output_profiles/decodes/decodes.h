//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
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
				struct Decodes : public Item
				{
				public:
					// Informal Option
					CFG_DECLARE_CONST_REF_GETTER_OF(GetThreadCount, _thread_count);
					CFG_DECLARE_CONST_REF_GETTER_OF(IsOnlyKeyframes, _only_keyframes);

				protected:
					void MakeList() override
					{
						Register<Optional>("ThreadCount", &_thread_count);
						Register<Optional>("OnlyKeyframes", &_only_keyframes);
					}

					int32_t _thread_count = 2;
					bool _only_keyframes  = false;
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg