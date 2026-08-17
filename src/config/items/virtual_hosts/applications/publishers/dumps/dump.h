//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "playlists.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct Dump : public Item
				{
				protected:
					ov::String _id;
					bool _enabled = false;
					ov::String _target_stream_name;
					mutable ov::Regex _target_stream_name_regex;
					ov::String _output_path;
					Playlists _playlists;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetId, _id);
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enabled);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTargetStreamName, _target_stream_name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPlaylists, _playlists.GetPlaylists());
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOutputPath, _output_path);

					ov::Regex GetTargetStreamNameRegex() const
					{
						if (_target_stream_name_regex.IsCompiled() == false)
						{
							_target_stream_name_regex = ov::Regex::CompiledRegex(ov::Regex::WildCardRegex(_target_stream_name));
						}

						return _target_stream_name_regex;
					}

					void ConfigureOutputPath(ov::String& output_path, ov::String v_host_name, ov::String app_name, ov::String stream_name)
					{
						if (output_path.IsEmpty())
						{
							return;
						}
						
						// ${VHostName}, ${AppName}, ${StreamName}
						output_path = output_path.Replace("${VHostName}", v_host_name.CStr());
						output_path = output_path.Replace("${AppName}", app_name.CStr());
						output_path = output_path.Replace("${StreamName}", stream_name.CStr());

						// ${YYYY}, ${MM}, ${DD}, ${hh}, ${mm}, ${ss}, ${ISO8601}, ${z}
						auto now = std::chrono::system_clock::now();
						auto time = std::chrono::system_clock::to_time_t(now);
						struct tm tm;
						::localtime_r(&time, &tm);

						char tmbuf[2048];

						// Replace ${ISO8601} to ISO8601 format
						memset(tmbuf, 0, sizeof(tmbuf));
						::strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%dT%H:%M:%S%z", &tm);
						output_path = output_path.Replace("${ISO8601}", tmbuf);

						// Replace ${YYYY}, ${MM}, ${DD}, ${hh}, ${mm}, ${ss}
						output_path = output_path.Replace("${YYYY}", ov::Converter::ToString(tm.tm_year + 1900).CStr());
						output_path = output_path.Replace("${MM}", ov::String::FormatString("%02d", tm.tm_mon + 1).CStr());
						output_path = output_path.Replace("${DD}", ov::String::FormatString("%02d", tm.tm_mday).CStr());
						output_path = output_path.Replace("${hh}", ov::String::FormatString("%02d", tm.tm_hour).CStr());
						output_path = output_path.Replace("${mm}", ov::String::FormatString("%02d", tm.tm_min).CStr());
						output_path = output_path.Replace("${ss}", ov::String::FormatString("%02d", tm.tm_sec).CStr());

						// +hhmm or -hhmm
						memset(tmbuf, 0, sizeof(tmbuf));
						strftime(tmbuf, sizeof(tmbuf), "%z", &tm);
						output_path = output_path.Replace("${z}", tmbuf);
					}

				protected:
					void MakeList() override
					{
						Register<Optional>("Id", &_id, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   if (_id.IsEmpty())
											   {
												   _id = ov::Random::GenerateString(8);
											   }
											   return nullptr;
										   });

						Register("Enable", &_enabled);
						Register("TargetStreamName", &_target_stream_name);
						Register("OutputPath", &_output_path);
						Register<Optional>("Playlists", &_playlists);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
