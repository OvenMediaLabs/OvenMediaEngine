#include "record.h"

#include <base/ovlibrary/cron.h>
#include <base/ovlibrary/ovlibrary.h>

#include <random>

#include "stream.h"

namespace info
{
	Record::Record()
	{
		// _stream = nullptr;

		_created_time = std::chrono::system_clock::now();
		_id = "";
		_is_config = false;
		_metadata = "";
		_transaction_id = "";

		_vhost_name = "";
		_application_name = "";
		_stream_name = "";
		_selected_track_ids.clear();
		_selected_variant_names.clear();

		_tmp_path = "";
		_file_path = "";
		_file_path_by_user = false;
		_info_path = "";
		_info_path_by_user = false;

		_record_bytes = 0;
		_record_time = 0;
		_record_total_bytes = 0;
		_record_total_time = 0;

		_sequence = 0;
		_interval = 0;
		_schedule = "";
		_segmentation_rule = "";

		_session_id = 0;

		_state = RecordState::Ready;
	}

	void Record::SetTransactionId(ov::String transaction_id)
	{
		_transaction_id = transaction_id;
	}
	ov::String Record::GetTransactionId()
	{
		return _transaction_id;
	}

	void Record::SetId(ov::String record_id)
	{
		_id = record_id;
	}

	ov::String Record::GetId() const
	{
		return _id;
	}
	
	void Record::SetByConfig(bool is_config)
	{
		_is_config = is_config;
	}

	bool Record::IsByConfig()
	{
		return _is_config;
	}

	void Record::SetMetadata(ov::String metadata)
	{
		_metadata = metadata;
	}
	ov::String Record::GetMetadata() const
	{
		return _metadata;
	}

	void Record::SetEnable(bool eanble)
	{
		_enable = eanble;
	}
	bool Record::GetEnable()
	{
		return _enable;
	}

	void Record::SetVhost(ov::String value)
	{
		_vhost_name = value;
	}
	ov::String Record::GetVhost()
	{
		return _vhost_name;
	}

	void Record::SetApplication(ov::String value)
	{
		_application_name = value;
	}

	ov::String Record::GetApplication()
	{
		return _application_name;
	}

	void Record::SetStreamName(ov::String stream_name) {
		_stream_name = stream_name;
	}

	ov::String Record::GetStreamName()
	{
		return _stream_name;
	}

	void Record::AddTrackId(uint32_t selected_id) 
	{
		_selected_track_ids.push_back(selected_id);
	}

	void Record::AddVariantName(ov::String selected_name)
	{
		_selected_variant_names.push_back(selected_name);
	}

	void Record::SetTrackIds(const std::vector<uint32_t>& ids)
	{
		_selected_track_ids.clear();
		_selected_track_ids.assign( ids.begin(), ids.end() ); 
	}

	void Record::SetVariantNames(const std::vector<ov::String>& names)
	{
		_selected_variant_names.clear();
		_selected_variant_names.assign( names.begin(), names.end() ); 
	}

	const std::vector<uint32_t>& Record::GetTrackIds() 
	{
		return _selected_track_ids;
	}

	const std::vector<ov::String>& Record::GetVariantNames()
	{
		return _selected_variant_names;
	}

	void Record::SetRemove(bool value)
	{
		_remove = value;
	}

	bool Record::GetRemove()
	{
		return _remove;
	}

	void Record::SetSessionId(session_id_t id)
	{
		_session_id = id;
	}

	session_id_t Record::GetSessionId()
	{
		return _session_id;
	}

	const std::chrono::system_clock::time_point &Record::GetCreatedTime() const
	{
		return _created_time;
	}
	void Record::SetInterval(int32_t interval)
	{
		_interval = interval;
	}
	int32_t Record::GetInterval()
	{
		return _interval;
	}

	void Record::SetSchedule(ov::String schedule)
	{
		_schedule = schedule;
	}

	ov::String Record::GetSchedule()
	{
		return _schedule;
	}

	void Record::SetSegmentationRule(ov::String rule)
	{
		_segmentation_rule = rule;
	}

	ov::String Record::GetSegmentationRule()
	{
		return _segmentation_rule;
	}

	const std::chrono::system_clock::time_point &Record::GetNextScheduleTime() const
	{
		return _schedule_next;
	}

	bool Record::IsNextScheduleTimeEmpty()
	{
		return (_schedule_next.time_since_epoch().count() == 0) ? true : false;
	}

	void Record::SetNextScheduleTime(std::chrono::system_clock::time_point &next)
	{
		_schedule_next = next;
	}

	bool Record::UpdateNextScheduleTime()
	{
		try
		{
			ov::String cron_expr = ov::String::FormatString("%s * * ?", GetSchedule().CStr());

			auto cron = ov::Cron::Make(cron_expr.CStr());
			std::time_t next = ov::Cron::Next(cron, std::time(0));
			std::chrono::system_clock::time_point next_time = std::chrono::system_clock::from_time_t(next);

			SetNextScheduleTime(next_time);
		}
		catch (ov::Cron::Exception const &ex)
		{
			loge("Record", "%s", ex.what());
			return false;
		}

		return true;
	}

	void Record::SetFilePath(ov::String file_path)
	{
		_file_path = file_path;
	}

	void Record::SetInfoPath(ov::String info_path)
	{
		_info_path = info_path;
	}
	void Record::SetFilePathSetByUser(bool by_user)
	{
		_file_path_by_user = by_user;
	}
	bool Record::IsFilePathSetByUser()
	{
		return _file_path_by_user;
	}
	void Record::SetInfoPathSetByUser(bool by_user)
	{
		_info_path_by_user = by_user;
	}

	void Record::SetTmpPath(ov::String tmp_path)
	{
		_tmp_path = tmp_path;
	}

	void Record::SetOutputFilePath(ov::String output_file_path)
	{
		_output_file_path = output_file_path;
	}

	ov::String Record::GetOutputFilePath()
	{
		return _output_file_path;
	}

	void Record::SetOutputInfoPath(ov::String output_info_path)
	{
		_output_info_path = output_info_path;
	}

	ov::String Record::GetOutputInfoPath()
	{
		return _output_info_path;
	}

	void Record::SetRecordBytes(uint64_t bytes)
	{
		_record_bytes = bytes;
	}
	void Record::SetRecordTotalBytes(uint64_t bytes)
	{
		_record_total_bytes = bytes;
	}
	void Record::SetRecordTime(uint64_t time)
	{
		_record_time = time;
	}
	void Record::SetRecordTotalTime(uint64_t time)
	{
		_record_total_bytes = time;
	}
	void Record::SetSequence(uint64_t sequence)
	{
		_sequence = sequence;
	}
	void Record::SetCreatedTime(std::chrono::system_clock::time_point tp)
	{
		_created_time = tp;
	}
	void Record::SetRecordStartTime(std::chrono::system_clock::time_point tp)
	{
		_record_start_time = tp;
	}
	void Record::SetRecordStopTime(std::chrono::system_clock::time_point tp)
	{
		_record_stop_time = tp;
	}
	bool Record::IsInfoPathSetByUser()
	{
		return _info_path_by_user;
	}
	void Record::IncreaseRecordBytes(uint64_t bytes)
	{
		_record_bytes += bytes;
	}
	void Record::UpdateRecordTime()
	{
		_record_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - _record_start_time).count();
	}
	void Record::IncreaseSequence()
	{
		_sequence++;
		_record_total_bytes += _record_bytes;
		_record_bytes = 0;
		_record_total_time += _record_time;
		_record_time = 0;
	}
	void Record::UpdateRecordStartTime()
	{
		_record_start_time = std::chrono::system_clock::now();
	}
	void Record::UpdateRecordStopTime()
	{
		_record_stop_time = std::chrono::system_clock::now();
	}

	ov::String Record::GetFilePath()
	{
		return _file_path;
	}
	ov::String Record::GetTmpPath()
	{
		return _tmp_path;
	}
	ov::String Record::GetInfoPath()
	{
		return _info_path;
	}
	uint64_t Record::GetRecordBytes()
	{
		return _record_bytes;
	}

	uint64_t Record::GetRecordTime()
	{
		return _record_time;
	}
	uint64_t Record::GetRecordTotalBytes()
	{
		return _record_total_bytes + _record_bytes;
	}
	uint64_t Record::GetRecordTotalTime()
	{
		return _record_total_time + _record_time;
	}
	uint64_t Record::GetSequence()
	{
		return _sequence;
	}
	const std::chrono::system_clock::time_point Record::GetRecordStartTime() const
	{
		return _record_start_time;
	}
	const std::chrono::system_clock::time_point Record::GetRecordStopTime() const
	{
		return _record_stop_time;
	}
	Record::RecordState Record::GetState()
	{
		return _state;
	}
	void Record::SetState(Record::RecordState state)
	{
		_state = state;
	}
	ov::String Record::GetStateString()
	{
		switch (GetState())
		{
			case RecordState::Ready:
				return "ready";
			case RecordState::Recording:
				return "recording";
			case RecordState::Stopping:
				return "stopping";
			case RecordState::Stopped:
				return "stopped";
			case RecordState::Error:
				return "error";
		}

		return "Unknown";
	}

	void Record::Clone(std::shared_ptr<info::Record> &record)
	{
	}

	const ov::String Record::GetInfoString()
	{
		ov::String info = "";

		info.AppendFormat(" id(%s)", _id.CStr());
		info.AppendFormat(" stream(%s)", _stream_name.CStr());
		info.AppendFormat(" sequence(%d)", _sequence);
		info.AppendFormat(" tmp_path(%s)", _tmp_path.CStr());
		info.AppendFormat(" file_path(%s)", _output_file_path.CStr());
		info.AppendFormat(" info_path(%s)", _output_info_path.CStr());
		info.AppendFormat(" total_bytes(%" PRIu64 ")", _record_total_bytes);
		info.AppendFormat(" total_time(%" PRIu64 ")", _record_total_time);
		info.AppendFormat(" created_time(%s)", ov::Converter::ToString(_created_time).CStr());
		info.AppendFormat(" start_time(%s)", ov::Converter::ToString(_record_start_time).CStr());
		info.AppendFormat(" stop_time(%s)", ov::Converter::ToString(_record_stop_time).CStr());
		info.AppendFormat(" interval(%d)", _interval);
		info.AppendFormat(" schedule(%s)", _schedule.CStr());
		info.AppendFormat(" segmentation(%s)", _segmentation_rule.CStr());
		info.AppendFormat(" variant_name(%s)", _selected_variant_names.size() > 0 ? ov::String::Join(_selected_variant_names, ",").CStr() : "all");

		return info;
	}

}  // namespace info