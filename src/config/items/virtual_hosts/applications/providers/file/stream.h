//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
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
			namespace pvd
			{
				namespace file
				{
					struct Stream : public Item
					{
					protected:
						ov::String _name;
						ov::String _path;

					public:
						CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name)
						CFG_DECLARE_CONST_REF_GETTER_OF(GetPath, _path)

					protected:
						void MakeList() override
						{
							Register("Name", &_name);
							Register("Path", &_path);
						}
					};
				}  // namespace file
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg