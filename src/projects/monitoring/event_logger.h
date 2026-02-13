#pragma once

#include "base/ovlibrary/log_write.h"
#include "base/ovlibrary/ovlibrary.h"
#include "event.h"

#define DEFAULT_EVENT_LOG_FILE_NAME "events.log"

namespace mon
{
	class EventLogger
	{
	public:
		EventLogger();
		void SetLogPath(const ov::String &log_path);

		//TODO(Getroot): Later, it may need to queue events
		void Write(const Event &event);

	private:
		ov::LogWrite _log_writer;
	};
}  // namespace mon