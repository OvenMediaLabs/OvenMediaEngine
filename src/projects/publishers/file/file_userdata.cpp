#include "file_userdata.h"

#include <base/info/stream.h>
#include <base/publisher/stream.h>

#include "file_private.h"

namespace pub
{
	FileUserdataSets::FileUserdataSets()
	{
	}

	FileUserdataSets::~FileUserdataSets()
	{
		_sets.clear();
	}

	bool FileUserdataSets::Set(ov::String record_id, const std::shared_ptr<info::Record> &record_info)
	{
		std::lock_guard<std::shared_mutex> lock(_mutex);

		auto [it, inserted] = _sets.emplace(record_id, record_info);
		return inserted;
	}

	
	std::vector<std::shared_ptr<info::Record>> FileUserdataSets::GetAll()
	{
		std::vector<std::shared_ptr<info::Record>> results;

		std::shared_lock<std::shared_mutex> lock(_mutex);

		for (auto &item : _sets)
		{
			results.push_back(item.second);
		}

		return results;
	}

	std::shared_ptr<info::Record> FileUserdataSets::GetByKey(ov::String record_id)
	{
		std::shared_lock<std::shared_mutex> lock(_mutex);

		auto iter = _sets.find(record_id);
		if (iter == _sets.end())
		{
			return nullptr;
		}

		return iter->second;
	}

	std::vector<std::shared_ptr<info::Record>> FileUserdataSets::GetByStreamName(ov::String stream_name)
	{
		std::vector<std::shared_ptr<info::Record>> results;

		std::shared_lock<std::shared_mutex> lock(_mutex);

		for (auto& [record_id, record_info] : _sets)
		{
			if (record_info->GetStreamName() == stream_name)
			{
				results.push_back(record_info);
			}
		}

		return results;
	}

	std::shared_ptr<info::Record> FileUserdataSets::GetBySessionId(session_id_t session_id)
	{
		std::shared_lock<std::shared_mutex> lock(_mutex);

		for (auto& item : _sets)
		{
			auto record_info = item.second;

			if (record_info->GetSessionId() == session_id)
				return record_info;
		}

		return nullptr;
	}

	std::shared_ptr<info::Record> FileUserdataSets::GetAt(uint32_t index)
	{
		std::shared_lock<std::shared_mutex> lock(_mutex);

		auto iter = _sets.begin();

		std::advance(iter, index);

		if (iter == _sets.end())
		{
			return nullptr;
		}

		return iter->second;
	}

	void FileUserdataSets::DeleteByKey(ov::String record_id)
	{
		std::lock_guard<std::shared_mutex> lock(_mutex);

		_sets.erase(record_id);
	}

	uint32_t FileUserdataSets::GetCount()
	{
		std::shared_lock<std::shared_mutex> lock(_mutex);

		return _sets.size();
	}
}  // namespace pub