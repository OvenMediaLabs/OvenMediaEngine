#pragma once

#include <string>

#include "./application.h"
#include "./host.h"
#include "./name_path.h"
#include "./stream.h"
#include "base/common_types.h"
#include "base/ovlibrary/enable_shared_from_this.h"

typedef uint32_t session_id_t;

namespace info
{
	class Stream;

	class Session : public ov::EnableSharedFromThis<info::Session>
	{
	public:
		Session(const info::Stream &stream);
		explicit Session(const info::Stream &stream, session_id_t session_id);
		Session(const info::Stream &stream, const Session &T);
		Session(Session &&T) = default;
		~Session() override	 = default;

		session_id_t GetId() const;
		ov::String GetUUID() const;

		NamePath GetNamePath() const;

		void SetName(const ov::String &name);
		const std::optional<ov::String> &GetName() const;

		const std::chrono::system_clock::time_point &GetCreatedTime() const;
		uint64_t GetSentBytes();

		const Stream &GetStream() const
		{
			return *_stream_info;
		}

		info::host_id_t GetHostId() const;
		info::application_id_t GetApplicationId() const;
		info::stream_id_t GetStreamId() const;

		struct Path
        {
            info::host_id_t _host_id;
			ov::String _host_name;
            info::application_id_t _application_id;
            info::stream_id_t _stream_id;
            session_id_t _session_id;
        };

		Path GetSessionPath() const;

	protected:
		uint64_t _sent_bytes	 = 0;
		uint64_t _received_bytes = 0;

		void UpdateNamePath();
		void SetIds(const info::Stream &stream);

	private:
		mutable std::mutex _name_path_mutex;
		NamePath _name_path;

		session_id_t _id;
		std::optional<ov::String> _name;
		std::chrono::system_clock::time_point _created_time;
		std::shared_ptr<info::Stream> _stream_info;

		info::host_id_t _host_id;
		info::application_id_t _application_id;
		info::stream_id_t _stream_id;
	};
}  // namespace info