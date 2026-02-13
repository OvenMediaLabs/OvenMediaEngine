//==============================================================================
//
//  PullStream Properties Class
//
//  Created by Keukhan
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once
#include "../../ovlibrary/string.h"

namespace pvd
{
	class PullStreamProperties
	{
	public:
		PullStreamProperties(){};

		bool IsPersistent()
		{
			return _persistent;
		}

		bool IsFailback()
		{
			return _failback;
		}

		bool IsRelay() 
		{
			return _relay;
		}

		bool IsFromOriginMapStore() 
		{
			return _from_origin_map_store;
		}

		bool IsRtcpSRTimestampIgnored()
		{
			return _ignore_rtcp_sr_timestamp;
		}

		void EnableFailback(bool failback)
		{
			_failback = failback;
		}

		void EnablePersistent(bool persistent)
		{
			_persistent = persistent;
		}

		void EnableRelay(bool relay)
		{
			_relay = relay;
		}

		void EnableFromOriginMapStore(bool from_origin_map_store)
		{
			_from_origin_map_store = from_origin_map_store;
		}

		void EnableIgnoreRtcpSRTimestamp (bool ignore_flag)
		{
			_ignore_rtcp_sr_timestamp = ignore_flag;
		}

		int32_t GetFailbackTimeout()
		{
			return _failback_timeout;
		}

		int32_t GetNoInputFailoverTimeout()
		{
			return _no_input_failover_timeout;
		}

		int32_t GetUnusedStreamDeletionTimeout()
		{
			return _unused_stream_deletion_timeout;
		}

		int32_t GetRetryCount() 
		{
			return _retry_count;
		}

		void SetRetryCount(int32_t retry_connect_count) 
		{
			_retry_count = retry_connect_count;
		}

		int32_t GetNoInputFailoverTimeoutMSec()
		{
			return _no_input_failover_timeout;
		}

		int32_t GetUnusedStreamDeletionTimeoutMSec()
		{
			return _unused_stream_deletion_timeout;
		}

		int32_t GetStreamFailbackTimeoutMSec()
		{
			return _failback_timeout;
		}

		// Setter
		void SetNoInputFailoverTimeout(int32_t milliseconds)
		{
			_no_input_failover_timeout = milliseconds;
		}

		void SetUnusedStreamDeletionTimeout(int32_t milliseconds)
		{
			_unused_stream_deletion_timeout = milliseconds;
		}

		void SetStreamFailbackTimeout(int32_t milliseconds)
		{
			_failback_timeout = milliseconds;
		}

	private:
		bool _persistent = false;
		bool _failback = false;
		bool _relay = false;
		bool _from_origin_map_store = false;
		bool _ignore_rtcp_sr_timestamp = false;

		// -1 means that the values in configuration file will be used. (Conf/Origins/Properties)
		int32_t _failback_timeout = -1;
		int32_t _no_input_failover_timeout = -1;
		int32_t _unused_stream_deletion_timeout = -1;
		int32_t _retry_count = -1; 

	public:
		// The last checked time is saved for failback.
		// TODO: However, it doesn't seem suitable for use in this class. This should be moved to another class.
		void UpdateFailbackCheckTime()
		{
			_last_failback_check_time = std::chrono::system_clock::now();
		}

		std::chrono::system_clock::time_point GetLastFailbackCheckTime()
		{
			return _last_failback_check_time;
		}

	private:
		std::chrono::system_clock::time_point _last_failback_check_time;
	};

}  // namespace pvd