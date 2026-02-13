#pragma once

#include <base/common_types.h>
#include <base/info/session.h>
#include <base/publisher/application.h>

#include "file_stream.h"
#include "file_userdata.h"

namespace pub
{
	class FileApplication final : public pub::Application
	{
	public:
		static std::shared_ptr<FileApplication> Create(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info);
		FileApplication(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info);
		~FileApplication() final;

	private:
		bool Start() override;
		bool Stop() override;

		// Application Implementation
		std::shared_ptr<pub::Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count) override;
		bool DeleteStream(const std::shared_ptr<info::Stream> &info) override;

		// By reading XML stored in the FILE.Record.RecordInfo path, Record Info to be automatically recorded is created.
		std::vector<std::shared_ptr<info::Record>> GetRecordInfoFromFile(const ov::String &file_path, const std::shared_ptr<info::Stream> &info);

	public:
		void SessionUpdateByUser();
		void SessionUpdateByStream(std::shared_ptr<FileStream> stream, bool stopped);
		void SessionUpdate(std::shared_ptr<FileStream> stream, std::shared_ptr<info::Record> userdata);
		void SessionStart(std::shared_ptr<FileSession> session);
		void SessionStop(std::shared_ptr<FileSession> session);

	public:
		std::shared_ptr<ov::Error> RecordStart(const std::shared_ptr<info::Record> record);
		std::shared_ptr<ov::Error> RecordStop(const std::shared_ptr<info::Record> record);
		std::shared_ptr<ov::Error> GetRecords(const std::shared_ptr<info::Record> record_query, std::vector<std::shared_ptr<info::Record>> &results);

	private:
		FileUserdataSets _record_info_list;
	};
}  // namespace pub