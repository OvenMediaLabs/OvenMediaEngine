//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"
#include "base/info/dump.h"

namespace mdl
{
	class Dump : public info::Dump
	{
	public:
		Dump();
		Dump(const info::Dump &info);
		Dump(const std::shared_ptr<info::Dump> &info);
		bool DumpData(const ov::String &file_name, const std::shared_ptr<const ov::Data> &data, bool append = false);
		bool CompleteDump();

		bool HasExtraData(const int32_t &id)
		{
			return _extra_datas.find(id) != _extra_datas.end();
		}

		uint32_t GetFirstSegmentNumber(const int32_t &id)
		{
			return _extra_datas[id];
		}

		void SetExtraData(const int32_t &id, const uint32_t &data)
		{
			_extra_datas[id] = data;
		}

	private:
		bool DumpToFile(const ov::String &path, const ov::String &file_name, const std::shared_ptr<const ov::Data> &data, bool add_history = true, bool append = false);
		bool MakeDumpInfo(ov::String &dump_info);

		struct DumpHistory
		{
			DumpHistory(const ov::String file_name)
			{
				this->file_name = file_name;
				this->time = std::chrono::system_clock::now();
			}

			// dump file full path
			ov::String file_name;
			// dump time
			std::chrono::system_clock::time_point time;
		};
		std::vector<DumpHistory> _dump_history_map;

		std::map<int32_t, uint32_t> _extra_datas;
	};
}  // namespace mdl