//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "dump.h"
#include "common.h"

namespace serdes
{
	std::shared_ptr<info::Dump> DumpInfoFromJson(const Json::Value &json_body)
	{
		auto dump = std::make_shared<info::Dump>();

		// id
		auto json_id = json_body["id"];
		if (json_id.empty() == false && json_id.isString() == true)
		{
			dump->SetId(json_id.asString().c_str());
		}

		// outputStreamName
		auto json_output_stream_name = json_body["outputStreamName"];
		if (json_output_stream_name.empty() == false && json_output_stream_name.isString() == true)
		{
			dump->SetStreamName(json_output_stream_name.asString().c_str());
		}

		// outputPath
		auto json_output_path = json_body["outputPath"];
		if (json_output_path.empty() == false && json_output_path.isString() == true)
		{
			dump->SetOutputPath(json_output_path.asString().c_str());
		}

		// infoFilePath
		auto json_info_file_path = json_body["infoFile"];
		if (json_info_file_path.empty() == false && json_info_file_path.isString() == true)
		{
			dump->SetInfoFile(json_info_file_path.asString().c_str());
		}

		// playlist
		auto json_playlist = json_body["playlist"];
		if (json_playlist.empty() == false || json_playlist.isArray() == true)
		{
			for (uint32_t i = 0; i < json_playlist.size(); i++)
			{
				if (json_playlist[i].isString())
				{
					dump->AddPlaylist(json_playlist[i].asString().c_str());
				}
			}
		}

		// userData
		auto json_user_data = json_body["userData"];
		if (json_user_data.empty() == false && json_user_data.isString() == true)
		{
			dump->SetUserData(json_user_data.asString().c_str());
		}

		return dump;
	}

	Json::Value JsonFromDumpInfo(const std::shared_ptr<const info::Dump> &record)
	{
		Json::Value response(Json::ValueType::objectValue);

		SetString(response, "id", record->GetId(), Optional::False);
		SetString(response, "outputStreamName", record->GetStreamName(), Optional::False);
		SetString(response, "outputPath", record->GetOutputPath(), Optional::False);
		SetString(response, "infoFile", record->GetInfoFileUrl(), Optional::True);
		SetString(response, "userData", record->GetUserData(), Optional::True);
		SetBool(response, "enabled", record->IsEnabled());

		// playlist	
		if (record->GetPlaylists().size() > 0)
		{
			Json::Value json_playlist(Json::ValueType::arrayValue);
			for (const auto &playlist : record->GetPlaylists())
			{
				json_playlist.append(playlist.CStr());
			}

			response["playlist"] = json_playlist;
		}

		return response;
	}

}  // namespace serdes