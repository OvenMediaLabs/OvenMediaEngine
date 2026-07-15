//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace prst
			{
				struct Stream : public Item
				{
				protected:
					ov::String _name;
					ov::String _origin_stream_name	 = "*";
					ov::String _fallback_stream_name = "";

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOriginStreamName, _origin_stream_name)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFallbackStreamName, _fallback_stream_name)

				protected:
					void MakeList() override
					{
						Register("Name", &_name);
						Register<Optional>("OriginStreamName", &_origin_stream_name);
						Register<Optional>("FallbackStreamName", &_fallback_stream_name);
					}
				};
			}  // namespace prst
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
