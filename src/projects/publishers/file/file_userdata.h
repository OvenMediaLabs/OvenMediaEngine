#pragma once

#include "base/info/record.h"
#include "base/ovlibrary/ovlibrary.h"

namespace pub
{
	class FileUserdataSets
	{
	public:
		FileUserdataSets();
		~FileUserdataSets();

		bool Set(ov::String record_id, std::shared_ptr<info::Record> record_info);

		uint32_t GetCount();

		std::shared_ptr<info::Record> GetAt(uint32_t index);
		std::shared_ptr<info::Record> GetByKey(ov::String record_id);
		std::vector<std::shared_ptr<info::Record>> GetByStreamName(ov::String stream_name);
		std::shared_ptr<info::Record> GetBySessionId(session_id_t session_id);
		void DeleteByKey(ov::String record_id);

	private:
		std::shared_mutex _mutex;
		// <userdata_id, userdata>
		std::map<ov::String, std::shared_ptr<info::Record>> _sets;
	};
}  // namespace pub