//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include <base/ovlibrary/files.h>
#include "dump.h"

namespace mdl
{
	Dump::Dump()
	{

	}
	
	Dump::Dump(const info::Dump &info)
	{
		// copy
		SetId(info.GetId());
		SetStreamName(info.GetStreamName());
		SetUserData(info.GetUserData());
		SetEnabled(info.IsEnabled());
		SetOutputPath(info.GetOutputPath());
		SetInfoFile(info.GetInfoFileUrl());
		SetPlaylists(info.GetPlaylists());
	}

	Dump::Dump(const std::shared_ptr<info::Dump> &info)
		: Dump(*info)
	{

	}

	bool Dump::DumpData(const ov::String &file_name, const std::shared_ptr<const ov::Data> &data, bool append)
	{
		if (DumpToFile(GetOutputPath(), file_name, data, true, append) == false)
		{
			logw("DEBUG", "Could not dump data to file: %s/%s", GetOutputPath().CStr(), file_name.CStr());
			return false;
		}

		// Write DumpInfo
		if (GetInfoFileUrl().IsEmpty() == false)
		{
			ov::String dump_history;
			if (MakeDumpInfo(dump_history) == false)
			{
				logw("DEBUG", "Could not make dump info");
				return false;
			}

			if (DumpToFile(GetInfoFilePath(), GetInfoFileName(), dump_history.ToData(false), false) == false)
			{
				logw("DEBUG", "Could not dump data to file: %s/%s", GetInfoFilePath().CStr(), GetInfoFileName().CStr());
				return false;
			}
		}

		return true;
	}

	bool Dump::CompleteDump()
	{
		SetEnabled(false);

		// Write DumpInfo
		if (GetInfoFileUrl().IsEmpty() == false)
		{
			ov::String dump_history;
			if (MakeDumpInfo(dump_history) == false)
			{
				logw("DEBUG", "Could not make dump info");
				return false;
			}

			if (DumpToFile(GetInfoFilePath(), GetInfoFileName(), dump_history.ToData(false), false) == false)
			{
				logw("DEBUG", "Could not dump data to file: %s/%s", GetInfoFilePath().CStr(), GetInfoFileName().CStr());
				return false;
			}
		}

		return true;
	}

	bool Dump::MakeDumpInfo(ov::String &dump_history)
	{
		/*
		<DumpInfo>
			<UserData>~~~</UserData>
			<Stream>/default/app/stream</Stream>
			<Status>Running | Completed | Error </Status>
			<Items>
				<Item>
					<Seq>0</Seq>
					<Time>~~~</Time>
					<File>~~~</File>
				</Item>
				...
				<Item>
					<Seq>1</Seq>
					<Time>~~~</Time>
					<File>/tmp/abc/xxx/298182/chunklist_0_video_llhls.m3u8</File>
				</Item>
				...
				<Item>
					<Seq>2</Seq>
					<Time>~~~</Time>
					<File>chunklist_0_video_llhls.m3u8</File>
				</Item>
			</Items>
		</DumpInfo>
		*/

		dump_history.Clear();
		dump_history = ov::String::FormatString("<DumpInfo>\n");
		dump_history.AppendFormat("\t<UserData>%s</UserData>\n", GetUserData().CStr());
		dump_history.AppendFormat("\t<Stream>%s</Stream>\n", GetStreamName().CStr());
		dump_history.AppendFormat("\t<Status>%s</Status>\n", IsEnabled() ? "Running" : "Completed");
		dump_history.AppendFormat("\t<Items>\n");

		uint32_t seq = 0;
		for (auto &item : _dump_history_map)
		{
			dump_history.AppendFormat("\t\t<Item>\n");
			dump_history.AppendFormat("\t\t\t<Seq>%d</Seq>\n", seq);
			dump_history.AppendFormat("\t\t\t<Time>%s</Time>\n", ov::Converter::ToISO8601String(item.time).CStr());
			dump_history.AppendFormat("\t\t\t<File>%s</File>\n", item.file_name.CStr());
			dump_history.AppendFormat("\t\t</Item>\n");

			seq ++;
		}

		dump_history.AppendFormat("\t</Items>\n");
		dump_history.AppendFormat("</DumpInfo>\n");

		return true;
	}

	bool Dump::DumpToFile(const ov::String &path, const ov::String &file_name, const std::shared_ptr<const ov::Data> &data, bool add_history, bool append)
	{
		if (ov::CreateDirectories(path) == false)
		{
			logw("DEBUG", "Could not create directories: %s", path.CStr());
			return false;
		}

		auto file_path_name = ov::PathManager::Combine(path, file_name);

		if (ov::DumpToFile(file_path_name, data, 0, append) == nullptr)
		{
			logw("DEBUG", "Could not dump data to file: %s", file_path_name.CStr());
			return false;
		}

		if (add_history == true)
		{
			_dump_history_map.emplace_back(file_path_name);
		}

		return true;
	}
}  // namespace mdl