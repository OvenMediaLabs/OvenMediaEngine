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
				struct FileRecord : public Item
				{
				protected:
					bool _enabled = false;
					ov::String _record_info_path;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enabled)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRecordInfo, _record_info_path)

				protected:
					void MakeList() override
					{
						Register<Optional>({"Enable", "enable"}, &_enabled);

						// RecordInfo is Deprecated
						Register<Optional>({"RecordInfo", "recordInfo"}, &_record_info_path, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   logw("Config", "Publishers.FILE.Record.RecordInfo will be deprecated. Please use Publishers.FILE.StreamMap.Path instead");
											   return nullptr;
										   });
						Register<Optional>({"Path", "path"}, &_record_info_path);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg