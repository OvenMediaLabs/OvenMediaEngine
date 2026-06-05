#pragma once

#include "base/ovlibrary/ovlibrary.h"

class LipSyncClock
{
public:
	LipSyncClock() = default;

	bool RegisterRtpClock(uint32_t id, double timebase);

	std::optional<uint64_t> CalcPTS(uint32_t id, uint32_t rtp_timestamp);
	bool UpdateSenderReportTime(uint32_t id, uint32_t ntp_msw, uint32_t ntp_lsw, uint32_t rtcp_timestamp);

	bool IsEnabled();

private:
	struct Clock
	{
		ov::Mutex    _lock;
		bool         _updated OV_GUARDED_BY(_lock) = false;
		bool         _first_sr OV_GUARDED_BY(_lock) = true;
		double       _timebase = 0;
		uint32_t     _last_rtcp_timestamp OV_GUARDED_BY(_lock) = 0;
		uint64_t     _extended_rtcp_timestamp OV_GUARDED_BY(_lock) = 0;
		uint64_t     _pts OV_GUARDED_BY(_lock) = 0;				// converted NTP timestamp to timebase timestamp
		uint32_t     _last_rtp_timestamp OV_GUARDED_BY(_lock) = 0;
		uint64_t     _extended_rtp_timestamp OV_GUARDED_BY(_lock) = 0;
	};

	ov::SharedMutex _map_lock;
	// Id, Clock
	std::map<uint32_t, std::shared_ptr<Clock>> _clock_map OV_GUARDED_BY(_map_lock);
	std::map<uint32_t, bool> _clock_enabled_map OV_GUARDED_BY(_map_lock);

	std::shared_ptr<Clock> GetClock(uint32_t id);
};