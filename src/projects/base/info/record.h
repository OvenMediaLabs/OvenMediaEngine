#pragma once

#include <string>

#include "base/common_types.h"
#include "base/ovlibrary/enable_shared_from_this.h"
#include "session.h"
#include "stream.h"
#include "vhost_app_name.h"

namespace info
{
	class Stream;

	class Record : public ov::EnableSharedFromThis<info::Record>
	{
	public:
		Record();
		~Record() override = default;

		void SetTransactionId(ov::String transaction_id);
		ov::String GetTransactionId();

		// set by user
		void SetId(ov::String id);
		ov::String GetId() const;

		void SetByConfig(bool is_config);
		bool IsByConfig();

		// set by user
		void SetMetadata(ov::String metadata);
		ov::String GetMetadata() const;

		// set by user
		void SetEnable(bool eanble);
		bool GetEnable();

		// set by user
		void SetVhost(ov::String vhost_name);
		ov::String GetVhost();

		// set by user
		void SetApplication(ov::String app_name);
		ov::String GetApplication();

		// set by user
		void SetStreamName(ov::String stream_name);
		ov::String GetStreamName();

		// set by user
		void AddTrackId(uint32_t selected_id);
		void AddVariantName(ov::String selected_name);

		void SetTrackIds(const std::vector<uint32_t>& ids);
		void SetVariantNames(const std::vector<ov::String>& names);

		const std::vector<uint32_t>& GetTrackIds();
		const std::vector<ov::String>& GetVariantNames();

		void SetRemove(bool value);
		bool GetRemove();

		void SetSessionId(session_id_t id);
		session_id_t GetSessionId();

		// set by user
		void SetInterval(int32_t interval);
		int32_t GetInterval();

		// set by user
		void SetSchedule(ov::String schedule);
		ov::String GetSchedule();
		const std::chrono::system_clock::time_point &GetNextScheduleTime() const;
		void SetNextScheduleTime(std::chrono::system_clock::time_point &next);
		bool IsNextScheduleTimeEmpty();
		bool UpdateNextScheduleTime();

		// set by user
		void SetSegmentationRule(ov::String rule);
		ov::String GetSegmentationRule();

		// set by user
		void SetFilePath(ov::String file_path);
		void SetFilePathTemplate(ov::String file_path);
		ov::String GetFilePath();

		// set by user
		void SetInfoPath(ov::String info_path);
		void SetInfoPathTemplate(ov::String file_path);
		ov::String GetInfoPath();

		void SetFilePathSetByUser(bool by_user);
		bool IsFilePathSetByUser();
		
		void SetInfoPathSetByUser(bool by_user);
		bool IsInfoPathSetByUser();

		void SetTmpPath(ov::String tmp_path);
		ov::String GetTmpPath();

		void SetOutputFilePath(ov::String output_file_path);
		ov::String GetOutputFilePath();

		void SetOutputInfoPath(ov::String output_info_path);
		ov::String GetOutputInfoPath();

		void IncreaseRecordBytes(uint64_t bytes);
		uint64_t GetRecordBytes();
		uint64_t GetRecordTotalBytes();
		void SetRecordBytes(uint64_t bytes);
		void SetRecordTotalBytes(uint64_t bytes);

		void UpdateRecordTime();
		uint64_t GetRecordTime();
		uint64_t GetRecordTotalTime();
		void SetRecordTime(uint64_t time);
		void SetRecordTotalTime(uint64_t time);

		void IncreaseSequence();

		void UpdateRecordStartTime();
		void UpdateRecordStopTime();

		uint64_t GetSequence();
		void SetSequence(uint64_t sequence);

		const std::chrono::system_clock::time_point &GetCreatedTime() const;
		const std::chrono::system_clock::time_point GetRecordStartTime() const;
		const std::chrono::system_clock::time_point GetRecordStopTime() const;
		void SetCreatedTime(std::chrono::system_clock::time_point tp);
		void SetRecordStartTime(std::chrono::system_clock::time_point tp);
		void SetRecordStopTime(std::chrono::system_clock::time_point tp);

		enum class RecordState : int8_t
		{
			Ready,
			Recording,
			Stopping,
			Stopped,
			Error
		};

		RecordState GetState();
		void SetState(RecordState state);
		ov::String GetStateString();

		void Clone(std::shared_ptr<info::Record> &record);

		const ov::String GetInfoString();

	private:
		ov::String _transaction_id;

		// User custom id
		ov::String _id;

		// by config
		// False - This is a recording task requested via the REST API, and it will remain active until a STOP command is issued through the REST API.
		// True - This is a recording task requested via the configuration file, and it will remain active until the configuration is changed.
		bool _is_config;
		
		// It is used as additional information for recording session. It's not essential information.
		ov::String _metadata;

		// Enabled/Disabled Flag
		bool _enable;

		// Remove Flag
		bool _remove;

		// Virtual Host
		ov::String _vhost_name;

		// Application
		ov::String _application_name;

		// Stream
		ov::String _stream_name;

		// The stream target for the Outbound that you want to record
		std::vector<uint32_t> _selected_track_ids;
		std::vector<ov::String> _selected_variant_names;

		// Path
		ov::String _file_path;
		bool _file_path_by_user;

		ov::String _info_path;
		bool _info_path_by_user;

		ov::String _output_file_path;
		ov::String _output_info_path;

		ov::String _tmp_path;

		// Recording interval
		//  0 : infinite
		//  > 0 : seconds
		int32_t _interval;

		// Split recording a schedule synchronized with system time, be similar to the cron-tap pattern
		//
		//  "<second> <minute> <hour>"
		//
		//   * : any value
		//   / : step values
		//   0-59 : allowed value of second and minute
		//   0-23 : allowed value of hour
		//
		// Examples
		// ---------------------------
		// * * *    : Every second
		// */30 * * : Every 30 seconds starting at 0 second after miniute
		// 30 * *   : At second 30 of every minitue
		// * 2 *    : Every second during minute 2 of every hour
		// * */2 *  : Every second past 2 minute starting at 0 minute after the hour
		// 0 0 1    : at 1:00:00 every day
		// 0 0 */1  : at second 0, at minute 0, every hour starting at 0 hour of every day

		// Recording schedule expr
		ov::String _schedule;
		std::chrono::system_clock::time_point _schedule_next;

		// Recorded (Accumulated) file size
		uint64_t _record_bytes;
		uint64_t _record_total_bytes;

		// Recorded (Accumulated) Time
		uint64_t _record_time;
		uint64_t _record_total_time;

		// Timestamp Rules for Split Recording
		//  continuity - The start of the split-recorded file PTS leads to the last PTS of the previously recorded file.
		//  discontiuity - The start PTS of the split-recorded file begins with zero.
		ov::String _segmentation_rule;

		// Sequence number of the recorded file
		uint32_t _sequence;

		// Event Occurrence Time
		std::chrono::system_clock::time_point _created_time;
		std::chrono::system_clock::time_point _record_start_time;
		std::chrono::system_clock::time_point _record_stop_time;

		RecordState _state;

		// File Session Id
		session_id_t _session_id;


	};
}  // namespace info