//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "publisher.h"
#include "stream_map/stream_map.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct FilePublisher : public Publisher
				{
					PublisherType GetType() const override
					{
						return PublisherType::File;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetFilePath, _file_path)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetInfoPath, _info_path)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRootPath, _root_path)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamMap, _stream_map)

				protected:
					void MakeList() override
					{
						Publisher::MakeList();

						Register<Optional>("RootPath", &_root_path);
						Register<Optional>("FilePath", &_file_path);
						Register<Optional>("InfoPath", &_info_path);

						// Record is Deprecated
						Register<Optional>({"Record", "record"}, &_stream_map, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   logw("Config", "Publishers.FILE.Record will be deprecated. Please use Publishers.FILE.StreamMap instead");
											   return nullptr;
										   });
						Register<Optional>({"StreamMap", "streamMap"}, &_stream_map);
					}

					ov::String _root_path = "";
					ov::String _file_path = "";
					ov::String _info_path = "";

					pub::StreamMap _stream_map;
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg