#include "push.h"

#include <base/ovlibrary/ovlibrary.h>

#include <random>

#include "stream.h"

namespace info
{
	Push::Push()
	{
		_created_time = std::chrono::system_clock::now();
		_id = "";

		_vhost_name = "";
		_application_name = "";
		_stream_name = "";
		_selected_track_ids.clear();
		_selected_variant_names.clear();
		_timestamp_mode = TimestampMode::ZeroBased;
		
		_protocol = "";
		_url = "";
		_stream_key = "";

		_Push_bytes = 0;
		_Push_time = 0;

		_Push_total_bytes = 0;
		_Push_total_time = 0;
		_sequence = 0;
		
		_session_id = 0;

		_is_config = false;

		_connection_timeout_ms = 5000; // 5 seconds
		_send_timeout_ms = 2000; // 2 seconds

		_state = PushState::Ready;
	}

	void Push::SetId(ov::String Push_id)
	{
		_id = Push_id;
	}

	ov::String Push::GetId() const
	{
		return _id;
	}

	void Push::SetEnable(bool enable)
	{
		_enable = enable;
	}
	bool Push::GetEnable()
	{
		return _enable;
	}

	void Push::SetVhost(ov::String value)
	{
		_vhost_name = value;
	}
	ov::String Push::GetVhost()
	{
		return _vhost_name;
	}

	void Push::SetApplication(ov::String value)
	{
		_application_name = value;
	}

	ov::String Push::GetApplication()
	{
		return _application_name;
	}

	void Push::SetStreamName(ov::String stream_name) {
		_stream_name = stream_name;
	}

	ov::String Push::GetStreamName()
	{
		return _stream_name;
	}

	void Push::AddTrackId(uint32_t selected_id) 
	{
		_selected_track_ids.push_back(selected_id);
	}

	void Push::AddVariantName(ov::String selected_name)
	{
		_selected_variant_names.push_back(selected_name);
	}

	void Push::SetTrackIds(const std::vector<uint32_t>& ids)
	{
		_selected_track_ids.clear();
		_selected_track_ids.assign( ids.begin(), ids.end() ); 
	}

	void Push::SetVariantNames(const std::vector<ov::String>& names)
	{
		_selected_variant_names.clear();
		_selected_variant_names.assign( names.begin(), names.end() ); 
	}

	const std::vector<uint32_t>& Push::GetTrackIds() 
	{
		return _selected_track_ids;
	}

	const std::vector<ov::String>& Push::GetVariantNames()
	{
		return _selected_variant_names;
	}

	void Push::SetRemove(bool value)
	{
		_remove = value;
	}

	bool Push::GetRemove()
	{
		return _remove;
	}

	void Push::SetTimestampMode(TimestampMode mode)
	{
		_timestamp_mode = mode;
	}

	TimestampMode Push::GetTimestampMode()
	{
		return _timestamp_mode;
	}

	void Push::SetSessionId(session_id_t id)
	{
		_session_id = id;
	}

	session_id_t Push::GetSessionId()
	{
		return _session_id;
	}

	const std::chrono::system_clock::time_point &Push::GetCreatedTime() const
	{
		return _created_time;
	}

	void Push::SetProtocol(ov::String protocol)
	{
		_protocol = protocol.Trim();
		_protocol.MakeLower();
	}
	ov::String Push::GetProtocol()
	{
		return _protocol;
	}

	Push::ProtocolType Push::GetProtocolType()
	{
		if (GetProtocol().LowerCaseString() == "rtmp")
		{
			return ProtocolType::RTMP;
		}
		else if (GetProtocol().LowerCaseString() == "srt")
		{
			return ProtocolType::SRT;
		}
		else if (GetProtocol().LowerCaseString() == "mpegts")
		{
			return ProtocolType::MPEGTS;
		}

		return ProtocolType::UNKNOWN;
	}


	void Push::SetUrl(ov::String url)
	{
		_url = url.Trim();
	}
	ov::String Push::GetUrl()
	{
		return _url;
	}

	void Push::SetStreamKey(ov::String stream_key)
	{
		_stream_key = stream_key.Trim();
	}
	ov::String Push::GetStreamKey()
	{
		return _stream_key;
	}

	void Push::IncreasePushBytes(uint64_t bytes)
	{
		_Push_bytes += bytes;
	}
	void Push::UpdatePushTime()
	{
		_Push_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - _Push_start_time).count();
	}
	void Push::IncreaseSequence()
	{
		_sequence++;
		_Push_total_bytes += _Push_bytes;
		_Push_bytes = 0;
		_Push_total_time += _Push_time;
		_Push_time = 0;
	}
	void Push::UpdatePushStartTime()
	{
		_Push_start_time = std::chrono::system_clock::now();
	}
	void Push::UpdatePushStopTime()
	{
		_Push_stop_time = std::chrono::system_clock::now();
	}

	uint64_t Push::GetPushBytes()
	{
		return _Push_bytes;
	}
	uint64_t Push::GetPushTime()
	{
		return _Push_time;
	}
	uint64_t Push::GetPushTotalBytes()
	{
		return _Push_total_bytes + _Push_bytes;
	}
	uint64_t Push::GetPushTotalTime()
	{
		return _Push_total_time + _Push_time;
	}
	uint64_t Push::GetSequence()
	{
		return _sequence;
	}
	const std::chrono::system_clock::time_point Push::GetPushStartTime() const
	{
		return _Push_start_time;
	}
	const std::chrono::system_clock::time_point Push::GetPushStopTime() const
	{
		return _Push_stop_time;
	}
	Push::PushState Push::GetState()
	{
		return _state;
	}
	void Push::SetState(Push::PushState state)
	{
		_state = state;
	}

	void Push::SetByConfig(bool is_config)
	{
		_is_config = is_config;
	}

	bool Push::IsByConfig()
	{
		return _is_config;
	}

	void Push::SetConnectionTimeout(int32_t timeout_ms)
	{
		_connection_timeout_ms = timeout_ms;
	}

	int32_t Push::GetConnectionTimeout()
	{
		return _connection_timeout_ms;
	}

	void Push::SetSendTimeout(int32_t timeout_ms)
	{
		_send_timeout_ms = timeout_ms;
	}
	
	int32_t Push::GetSendTimeout()
	{
		return _send_timeout_ms;
	}

	ov::String Push::GetStateString()
	{
		switch (GetState())
		{
			case PushState::Ready:
				return "ready";
			case PushState::Connecting:
				return "connecting";				
			case PushState::Pushing:
				return "pushing";
			case PushState::Stopping:
				return "stopping";
			case PushState::Stopped:
				return "stopped";
			case PushState::Error:
				return "error";
		}

		return "Unknown";
	}

	const ov::String Push::GetInfoString()
	{
		ov::String info = "";

		info.AppendFormat("uid(%s), vhost(%s) app(%s) stream(%s) -> protocol(%s) url(%s) streamKey(%s) variantNames(%s)",
						  _id.CStr(), GetVhost().CStr(), GetApplication().CStr(), GetStreamName().CStr(), GetProtocol().CStr(), GetUrl().CStr(), GetStreamKey().CStr(),
						  GetVariantNames().empty() ? "all" : ov::String::Join(GetVariantNames(), ",").CStr());

		return info;
	}

}  // namespace info