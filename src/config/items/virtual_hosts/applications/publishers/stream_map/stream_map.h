//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
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
			namespace pub
			{
				struct StreamMap : public Item
				{
				protected:
					bool _enabled = false;
					ov::String _path;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enabled)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPath, _path)

				protected:
					void MakeList() override
					{
						Register<Optional>({"Enable", "enable"}, &_enabled);
						Register<Optional>({"Path", "path"}, &_path);

						// RecordInfo is Deprecated
						Register<Optional>({"RecordInfo", "recordInfo"}, &_path, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   logw("Config", "Publishers.FILE.Record.RecordInfo will be deprecated. Please use Publishers.FILE.StreamMap.Path instead");
											   return nullptr;
										   });
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg