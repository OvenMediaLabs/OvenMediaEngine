#pragma once

#include "base/info/record.h"
#include "base/ovlibrary/ovlibrary.h"

namespace pub
{
	class FileExport : public ov::Singleton<FileExport>
	{
	public:
		FileExport();
		~FileExport();

		bool ExportRecordToXml(const ov::String path, const std::shared_ptr<info::Record> &record);

		std::shared_mutex _mutex;
		;
	};
}  // namespace pub