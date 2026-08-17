#include "event_logger.h"

namespace mon
{
	EventLogger::EventLogger()
		: _log_writer(DEFAULT_EVENT_LOG_FILE_NAME, true)
	{
	}

	void EventLogger::SetLogPath(const ov::String &log_path)
	{
		_log_writer.SetLogPath(log_path.CStr());
	}

	void EventLogger::Write(const Event &event)
	{
		_log_writer.Write(event.SerializeToJson().CStr(), event.GetCreationTimeMSec() / 1000);
	}
}  // namespace mon