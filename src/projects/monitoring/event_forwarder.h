#pragma once

#include "base/ovlibrary/ovlibrary.h"
#include "config/config.h"

#define SHIPPER_INFO_DB_FILE "forwarder.db"
#define OVEN_CONSOLE_AUTH_URL "https://ovenconsole.com/auth"

namespace mon
{
	class EventForwarder
	{
	public:
		void SetLogPath(const ov::String &log_path);
		bool Start(const std::shared_ptr<const cfg::Server> &server_config);
		bool Stop();

	private:
		bool AuthOvenConsole();
		void ForwarderThread();

		bool Forwarding(const ov::String &line);

		ov::String GetShipperInfoDBFilePath();
		// [result | file name | file offset]
		std::tuple<bool, std::time_t, uint64_t> LoadLastShippedInfo();
		bool StoreLastForwardedInfo(std::time_t, uint64_t file_offset);
		bool ConnectIfNeeded();
		bool Send();

		class EventLogFileFinder
		{
		public:
			EventLogFileFinder(const ov::String &log_dir_path);

			// Reset and set start offset
			void ResetStartOffset(std::time_t start_file_time, uint64_t start_file_offset);

			std::time_t GetOpenLogTime();

			bool IsCurrAvailable();
			bool IsNextAvailable();
			bool OpenNextEventLog(std::ifstream &ifs);

		private:
			std::tuple<bool, ov::String> GetLogFilePathByTime(std::time_t start_file_time);
			std::tuple<bool, std::time_t, ov::String> FindNextLogFilePath(std::time_t prev_file_time);

			ov::String _log_dir_path;

			std::time_t _curr_open_log_time = 0;
			ov::String _curr_open_log_file_path;
			ino_t _curr_open_inode_number = 0;
			std::time_t _start_file_time  = 0;
			uint64_t _start_file_offset	  = 0;
		};

		ov::String _user_key;

		bool _enabled = false;
		ov::String _collector;
		std::shared_ptr<ov::Url> _collector_url = nullptr;

		ov::String _log_path;

		std::shared_ptr<const cfg::Server> _server_config = nullptr;

		std::thread _shipper_thread;
		bool _run_thread = false;

		std::shared_ptr<ov::Socket> _socket;
	};
}  // namespace mon