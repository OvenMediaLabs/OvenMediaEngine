#include "event_forwarder.h"

#include <base/ovlibrary/files.h>
#include <base/ovlibrary/path_manager.h>
#include <modules/http/client/http_client.h>

#include "event_logger.h"
#include "monitoring_private.h"

namespace mon
{
	/******************************
	 * EventLogFileFinder
	 * ****************************/

	EventForwarder::EventLogFileFinder::EventLogFileFinder(const ov::String &log_dir_path)
	{
		_log_dir_path = log_dir_path;
	}

	void EventForwarder::EventLogFileFinder::ResetStartOffset(std::time_t start_file_time, uint64_t start_file_offset)
	{
		_curr_open_inode_number	 = 0;
		_curr_open_log_time		 = 0;
		_curr_open_log_file_path = "";

		_start_file_time		 = start_file_time;
		_start_file_offset		 = start_file_offset;
	}

	std::time_t EventForwarder::EventLogFileFinder::GetOpenLogTime()
	{
		return _curr_open_log_time;
	}

	// Check inode number
	bool EventForwarder::EventLogFileFinder::IsCurrAvailable()
	{
		if (_curr_open_inode_number == 0 || _curr_open_log_file_path.IsEmpty())
		{
			return false;
		}

		// Store inode number of the open file
		struct stat st;
		if (stat(_curr_open_log_file_path.CStr(), &st) != 0)
		{
			return false;
		}

		if (_curr_open_inode_number != st.st_ino)
		{
			return false;
		}

		return true;
	}

	bool EventForwarder::EventLogFileFinder::IsNextAvailable()
	{
		auto [result, time, path] = FindNextLogFilePath(_curr_open_log_time);
		return result;
	}

	bool EventForwarder::EventLogFileFinder::OpenNextEventLog(std::ifstream &ifs)
	{
		ov::String open_file_path;
		std::time_t open_file_time		= 0;
		std::streampos open_file_offset = 0;

		// First time
		if (_curr_open_log_time == 0)
		{
			// Start from the oldest file
			if (_start_file_offset == 0)
			{
				auto [result, file_time, file_path] = FindNextLogFilePath(0);
				if (result == true)
				{
					open_file_path = file_path;
					open_file_time = file_time;
				}
				else
				{
					return false;
				}
			}
			// Start from set start_file_time by user
			else
			{
				// open file by start_file_time
				auto [result, file_path] = GetLogFilePathByTime(_start_file_time);
				if (result == true)
				{
					open_file_path	 = file_path;
					open_file_time	 = _start_file_time;
					open_file_offset = _start_file_offset;
				}
				// If there is no log file matched by start_file_time, open next file
				else
				{
					auto [next_result, next_file_time, next_file_path] = FindNextLogFilePath(_start_file_time);
					if (next_result == true)
					{
						open_file_path = next_file_path;
						open_file_time = next_file_time;
					}
					else
					{
						return false;
					}
				}
			}
		}
		else
		{
			auto [next_result, next_file_time, next_file_path] = FindNextLogFilePath(_curr_open_log_time);
			if (next_result == true)
			{
				open_file_path = next_file_path;
				open_file_time = next_file_time;
			}
			else
			{
				return false;
			}
		}

		if (ifs.is_open())
		{
			ifs.close();
		}

		ifs.open(open_file_path.CStr());
		ifs.seekg(open_file_offset);
		_curr_open_log_time		 = open_file_time;
		_curr_open_log_file_path = open_file_path;
		logti("Open log file for forwarding: %s", open_file_path.CStr());

		// Store inode number of the open file
		struct stat st;
		if (stat(open_file_path.CStr(), &st) == 0)
		{
			_curr_open_inode_number = st.st_ino;
		}

		return true;
	}

	std::tuple<bool, ov::String> EventForwarder::EventLogFileFinder::GetLogFilePathByTime(std::time_t time)
	{
		std::tm calendar{};
		::localtime_r(&time, &calendar);

		std::ostringstream os;
		// /var/log/ovenmediaengine/events.log.20210805
		os << _log_dir_path.CStr() << "/" << DEFAULT_EVENT_LOG_FILE_NAME << "." << std::put_time(&calendar, "%Y%m%d");

		ov::String file_path = os.str().c_str();

		if (::access(file_path.CStr(), F_OK) != -1)
		{
			return {true, file_path.CStr()};
		}

		return {false, ""};
	}

	std::tuple<bool, std::time_t, ov::String> EventForwarder::EventLogFileFinder::FindNextLogFilePath(std::time_t prev_file_time)
	{
		std::time_t next_file_time = 0;
		ov::String next_file_path;

		auto [result, file_list] = ov::GetFileList(_log_dir_path.CStr());

		if (result == false)
		{
			return {false, 0, ""};
		}

		for (const auto &entry : file_list)
		{
			struct stat stat_buf;
			if (stat(entry.CStr(), &stat_buf) == -1)
			{
				continue;
			}

			if (S_ISDIR(stat_buf.st_mode))
			{
				continue;
			}

			ov::String entry_file_path = entry.CStr();
			auto entry_file_path_items = entry_file_path.Split("/");
			auto entry_file_name	   = entry_file_path_items.back();

			// events.log.yyyymmdd
			auto file_name_items	   = entry_file_name.Split(".");
			if (file_name_items.size() != 3)
			{
				continue;
			}

			if (file_name_items[0] != "events" || file_name_items[1] != "log")
			{
				continue;
			}

			// yyyymmdd
			auto date = file_name_items[2];
			if (date.GetLength() != 8)
			{
				continue;
			}

			auto year			   = ov::Converter::ToUInt32(date.Left(4).CStr());
			auto mon			   = ov::Converter::ToUInt32(date.Substring(4, 2).CStr());
			auto day			   = ov::Converter::ToUInt32(date.Right(2).CStr());

			std::time_t entry_time = ov::Converter::ToTime(year, mon, day, 0, 0, false);

			// Find the oldest among files newer than prev_file_time
			if (entry_time > prev_file_time)
			{
				if (next_file_time == 0)
				{
					next_file_time = entry_time;
					next_file_path = entry_file_path;
				}
				else if (entry_time < next_file_time)
				{
					next_file_time = entry_time;
					next_file_path = entry_file_path;
				}
			}
		}

		if (next_file_time == 0)
		{
			return {false, 0, ""};
		}

		return {true, next_file_time, next_file_path};
	}

	/******************************
	 * EventLogFileFinder
	 * ****************************/

	void EventForwarder::SetLogPath(const ov::String &log_path)
	{
		_log_path = log_path;
	}

	bool EventForwarder::Start(const std::shared_ptr<const cfg::Server> &server_config)
	{
		if (server_config == nullptr)
		{
			return false;
		}

		_server_config			 = server_config;
		_user_key				 = _server_config->GetAnalytics().GetUserKey();

		bool is_collector_parsed = false;
		if (_server_config->GetAnalytics().GetForwarding().IsParsed())
		{
			_enabled   = _server_config->GetAnalytics().GetForwarding().IsEnabled();
			_collector = _server_config->GetAnalytics().GetForwarding().GetCollector(&is_collector_parsed);
		}

		if (_enabled == false)
		{
			logti("Event Forwarder is disabled in configuration");
			return false;
		}

		_collector_url = ov::Url::Parse(_collector);
		if (_collector_url == nullptr)
		{
			logte("Event Forwarder is disabled because an invalid URL is set : %s", _collector.CStr());
			return false;
		}

		if (_collector_url->Scheme().UpperCaseString() != "TCP")
		{
			logte("Event forwarder is disabled due to unsupported protocol. : %s", _collector.CStr());
			return false;
		}

		// This is for OvenConsole
		if (is_collector_parsed == false)
		{
			bool result = AuthOvenConsole();
			if (result == false)
			{
				logte("Event Forwarder is disabled due to OvenConsole authentication failure");
				return false;
			}
		}

		_run_thread		= true;
		_shipper_thread = std::thread(&EventForwarder::ForwarderThread, this);
		pthread_setname_np(_shipper_thread.native_handle(), "ForwarderThread");

		return true;
	}

	bool EventForwarder::Stop()
	{
		_run_thread = false;

		if (_shipper_thread.joinable())
		{
			_shipper_thread.join();
		}

		return true;
	}

	bool EventForwarder::AuthOvenConsole()
	{
		auto client = std::make_shared<http::clnt::HttpClient>();
		client->SetMethod(http::Method::Post);
		client->SetBlockingMode(ov::BlockingMode::Blocking);
		client->SetConnectionTimeout(3000);
		client->SetRequestHeader("Authorization", ov::String::FormatString("Bearer %s", _user_key.CStr()));
		client->Request(OVEN_CONSOLE_AUTH_URL, [=](http::StatusCode status_code, const std::shared_ptr<ov::Data> &data, const std::shared_ptr<const ov::Error> &error) {
			// A response was received from the server.
			if (error == nullptr)
			{
				if (status_code == http::StatusCode::OK)
				{
					return true;
				}
				else
				{
					// Receive Invalid Status Code
					return false;
				}
			}
			else
			{
				// A connection error or an error that does not conform to the HTTP spec has occurred.
				return false;
			}
		});

		return false;
	}

	void EventForwarder::ForwarderThread()
	{
		ov::logger::ThreadHelper thread_helper;

		// Get the last_shipped_log_time and file_offset
		auto [result, last_file_time, last_file_offset] = LoadLastShippedInfo();
		EventLogFileFinder event_stream(_log_path);

		if (result == true)
		{
			// Resume shipping
			event_stream.ResetStartOffset(last_file_time, last_file_offset);
		}
		else
		{
			// Start shipping from the oldest file
		}

		std::ifstream ifs;
		std::streampos pos = 0;
		while (_run_thread)
		{
			if (ifs.is_open() == false || (event_stream.IsNextAvailable() && ifs.eof()))
			{
				if (event_stream.OpenNextEventLog(ifs) == true)
				{
					pos = ifs.tellg();
				}
				else
				{
					// There is no events.log.xxxxxx
					sleep(1);
					continue;
				}
			}
			// Current file is moved or something failed
			else if (event_stream.IsCurrAvailable() == false)
			{
				event_stream.ResetStartOffset(event_stream.GetOpenLogTime(), pos);
				if (event_stream.OpenNextEventLog(ifs) == true)
				{
					pos = ifs.tellg();
				}
				else
				{
					// There is no current event log ? maybe it was removed, wait for new log file
					sleep(1);
					continue;
				}
			}
			else
			{
				// Wait for new message in current log file
				sleep(1);

				// Clear error bits to read a growing log file
				ifs.clear();
			}

			std::string line;
			while (std::getline(ifs, line) && _run_thread)
			{
				// Collector will use '\n' for delimiter
				line.append("\n");

				// end of file
				if (ifs.eof() == true || ifs.tellg() == -1)
				{
					pos += line.size();
				}
				else
				{
					pos = ifs.tellg();
				}

				// Ship the line
				while (_run_thread)
				{
					if (Forwarding(line.c_str()))
					{
						// Store last shipped info
						StoreLastForwardedInfo(event_stream.GetOpenLogTime(), pos);
						break;
					}
					else
					{
						// Keep trying until the transfer is successful.
						sleep(1);
					}
				}
			}
		}
	}

	bool EventForwarder::Forwarding(const ov::String &line)
	{
		if (ConnectIfNeeded() == true)
		{
			return _socket->Send(line.ToData(false));
		}
		else
		{
			return false;
		}

		return true;
	}

	bool EventForwarder::ConnectIfNeeded()
	{
		if (_collector_url->Scheme().UpperCaseString() == "TCP")
		{
			auto address = ov::SocketAddress::CreateAndGetFirst(_collector_url->Host().CStr(), _collector_url->Port());

			if (_socket == nullptr)
			{
				_socket = ov::SocketPool::GetTcpPool()->AllocSocket(address.GetFamily());
			}

			if (_socket->GetState() == ov::SocketState::Connected)
			{
				return true;
			}
			else
			{
				auto error = _socket->Connect(address);
				if (error != nullptr)
				{
					logtt("Could not connect to collector server : %s", _collector_url->ToUrlString(true).CStr());
					_socket = nullptr;

					return false;
				}

				return true;
			}
		}

		return false;
	}

	ov::String EventForwarder::GetShipperInfoDBFilePath()
	{
		return ov::PathManager::Combine(ov::PathManager::GetAppPath(), SHIPPER_INFO_DB_FILE);
	}

	// [result | last shipped log time | file offset]
	std::tuple<bool, std::time_t, uint64_t> EventForwarder::LoadLastShippedInfo()
	{
		auto db_file = GetShipperInfoDBFilePath();

		std::ifstream fs(db_file);
		if (!fs.is_open())
		{
			return {false, 0, 0};
		}

		std::string line;
		std::getline(fs, line);
		fs.close();

		line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

		// time_t of log file,offset
		// 1597844530,1283
		ov::String row = line.c_str();
		auto fields	   = row.Split(",");
		if (fields.size() != 2)
		{
			return {false, 0, 0};
		}

		return {true, ov::Converter::ToUInt32(fields[0].CStr()), ov::Converter::ToUInt64(fields[1].CStr())};
	}

	bool EventForwarder::StoreLastForwardedInfo(std::time_t file_time, uint64_t file_offset)
	{
		auto db_file = GetShipperInfoDBFilePath();

		std::ofstream fs(db_file);
		if (!fs.is_open())
		{
			return false;
		}

		auto row = ov::String::FormatString("%u,%" PRIu64, file_time, file_offset);

		fs.write(row.CStr(), row.GetLength());
		fs.close();

		return true;
	}
}  // namespace mon