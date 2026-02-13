#include "file_macro.h"

#include <base/info/application.h>
#include <base/info/host.h>
#include <base/info/stream.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/publisher/application.h>
#include <base/publisher/stream.h>
#include <config/config.h>

#include <regex>

#include "file_export.h"
#include "file_private.h"

namespace pub
{
	ov::String FileMacro::ConvertMacro(const std::shared_ptr<info::Application> &app, const std::shared_ptr<info::Stream> &stream, const std::shared_ptr<info::Record> record, const ov::String &src)
	{
		auto pub_config = app->GetConfig().GetPublishers();
		auto host_config = app->GetHostInfo();

		std::string raw_string = src.CStr();
		ov::String replaced_string = ov::String(raw_string.c_str());

		// =========================================
		// Definition of Macro
		// =========================================
		// ${StartTime:YYYYMMDDhhmmss}
		// ${EndTime:YYYYMMDDhhmmss}
		// 	 YYYY - year
		// 	 MM - month (00~12)
		// 	 DD - day (00~31)
		// 	 hh : hour (0~23)
		// 	 mm : minute (00~59)
		// 	 ss : second (00~59)
		// ${VirtualHost} :  Virtual Host Name
		// ${Application} : Application Name
		// ${Stream} : Stream name
		// ${Sequence} : Sequence number
		// ${Id} : Identification Code

		std::regex reg_exp("\\$\\{([a-zA-Z0-9:]+)\\}");
		const std::sregex_iterator it_end;
		for (std::sregex_iterator it(raw_string.begin(), raw_string.end(), reg_exp); it != it_end; ++it)
		{
			std::smatch matches = *it;
			std::string tmp;

			tmp = matches[0];
			ov::String full_match = ov::String(tmp.c_str());

			tmp = matches[1];
			ov::String group = ov::String(tmp.c_str());

			// logtt("Full Match(%s) => Group(%s)", full_match.CStr(), group.CStr());

			if (group.IndexOf("VirtualHost") != -1L)
			{
				replaced_string = replaced_string.Replace(full_match, host_config.GetName());
			}
			if (group.IndexOf("Application") != -1L)
			{
				// Delete Prefix virtualhost name. ex) #[VirtualHost]#Application
				ov::String prefix = ov::String::FormatString("#%s#", host_config.GetName().CStr());
				auto app_name = app->GetVHostAppName();
				auto application_name = app_name.ToString().Replace(prefix, "");

				replaced_string = replaced_string.Replace(full_match, application_name);
			}
			if (group.IndexOf("Stream") != -1L)
			{
				replaced_string = replaced_string.Replace(full_match, stream->GetName());
			}
			if (group.IndexOf("Sequence") != -1L)
			{
				ov::String buff = ov::String::FormatString("%d", record->GetSequence());

				replaced_string = replaced_string.Replace(full_match, buff);
			}
			if (group.IndexOf("Id") == 0)
			{
				ov::String buff = ov::String::FormatString("%s", record->GetId().CStr());

				replaced_string = replaced_string.Replace(full_match, buff);
			}
			if (group.IndexOf("TransactionId") == 0)
			{
				ov::String buff = ov::String::FormatString("%s", record->GetTransactionId().CStr());

				replaced_string = replaced_string.Replace(full_match, buff);
			}
			if (group.IndexOf("StartTime") != -1L)
			{
				time_t now = std::chrono::system_clock::to_time_t(record->GetRecordStartTime());
				struct tm timeinfo;
				if (localtime_r(&now, &timeinfo) == nullptr)
				{
					logtw("Could not get localtime");
					continue;
				}

				char buff[80];
				ov::String YYYY, MM, DD, hh, mm, ss;

				strftime(buff, sizeof(buff), "%Y", &timeinfo);
				YYYY = buff;
				strftime(buff, sizeof(buff), "%m", &timeinfo);
				MM = buff;
				strftime(buff, sizeof(buff), "%d", &timeinfo);
				DD = buff;
				strftime(buff, sizeof(buff), "%H", &timeinfo);
				hh = buff;
				strftime(buff, sizeof(buff), "%M", &timeinfo);
				mm = buff;
				strftime(buff, sizeof(buff), "%S", &timeinfo);
				ss = buff;

				ov::String str_time = group;
				str_time = str_time.Replace("StartTime:", "");
				str_time = str_time.Replace("YYYY", YYYY);
				str_time = str_time.Replace("MM", MM);
				str_time = str_time.Replace("DD", DD);
				str_time = str_time.Replace("hh", hh);
				str_time = str_time.Replace("mm", mm);
				str_time = str_time.Replace("ss", ss);

				replaced_string = replaced_string.Replace(full_match, str_time);
			}
			if (group.IndexOf("EndTime") != -1L)
			{
				time_t now = std::chrono::system_clock::to_time_t(record->GetRecordStopTime());
				struct tm timeinfo;
				if (localtime_r(&now, &timeinfo) == nullptr)
				{
					logtw("Could not get localtime");
					continue;
				}

				char buff[80];
				ov::String YYYY, MM, DD, hh, mm, ss;

				strftime(buff, sizeof(buff), "%Y", &timeinfo);
				YYYY = buff;
				strftime(buff, sizeof(buff), "%m", &timeinfo);
				MM = buff;
				strftime(buff, sizeof(buff), "%d", &timeinfo);
				DD = buff;
				strftime(buff, sizeof(buff), "%H", &timeinfo);
				hh = buff;
				strftime(buff, sizeof(buff), "%M", &timeinfo);
				mm = buff;
				strftime(buff, sizeof(buff), "%S", &timeinfo);
				ss = buff;

				ov::String str_time = group;
				str_time = str_time.Replace("EndTime:", "");
				str_time = str_time.Replace("YYYY", YYYY);
				str_time = str_time.Replace("MM", MM);
				str_time = str_time.Replace("DD", DD);
				str_time = str_time.Replace("hh", hh);
				str_time = str_time.Replace("mm", mm);
				str_time = str_time.Replace("ss", ss);

				replaced_string = replaced_string.Replace(full_match, str_time);
			}
		}

		// logtt("Regular Expression Result : %s", replaced_string.CStr());

		return replaced_string;
	}
}  // namespace pub
