#include "lip_sync_clock.h"
#include "base/ovlibrary/clock.h"
#define OV_LOG_TAG "LipSyncClock"

bool LipSyncClock::RegisterRtpClock(uint32_t id, double timebase)
{
	auto clock = std::make_shared<Clock>();
	clock->_timebase = timebase;
	std::lock_guard<std::shared_mutex> lock(_map_lock);
	_clock_map.emplace(id, clock);

	return true;
}

bool LipSyncClock::IsEnabled()
{
	std::shared_lock<std::shared_mutex> lock(_map_lock);
	return !_clock_map.empty() && _clock_enabled_map.size() == _clock_map.size();
}

std::shared_ptr<LipSyncClock::Clock> LipSyncClock::GetClock(uint32_t id)
{
	std::shared_lock<std::shared_mutex> lock(_map_lock);
	auto it = _clock_map.find(id);
	if (it == _clock_map.end())
	{
		return nullptr;
	}
	return it->second;
}

std::optional<uint64_t> LipSyncClock::CalcPTS(uint32_t id, uint32_t rtp_timestamp)
{
	auto clock = GetClock(id);
	if(clock == nullptr)
	{
		return {};
	}

	std::lock_guard<std::mutex> lock(clock->_lock);

	if(clock->_updated == false)
	{
		return {};
		// @Deprecated
		// TODO(Getroot) : This method sometimes causes a smaller PTS to come out after the RTCP SR arrives. 
		// Since this is not allowed in OME, we have to think more about how to handle it.
		// Since most servers send RTP and RTCP SR, 
		// it is the simplest and most powerful to wait for the RTCP SR and then calculate the PTS.

		// Update using wall clock until rtcp sr is arrived
		// uint32_t msw, lsw;
		// ov::Clock::GetNtpTime(msw, lsw);
		// UpdateSenderReportTime(id, msw, lsw, rtp_timestamp);
	}

	uint32_t delta = 0;
	if (clock->_last_rtp_timestamp == 0)
	{
		clock->_extended_rtp_timestamp = rtp_timestamp;
	}
	else
	{
		if (rtp_timestamp > clock->_last_rtp_timestamp)
		{
			delta = rtp_timestamp - clock->_last_rtp_timestamp;
		}
		else
		{
			delta = clock->_last_rtp_timestamp - rtp_timestamp;
			if (delta > 0x80000000)
			{
				// wrap around
				delta = 0xFFFFFFFF - clock->_last_rtp_timestamp + rtp_timestamp + 1;
			}
			else
			{
				// reordering or duplicate or error
				delta = 0;
				logtw("RTP timestamp is not monotonic: %u -> %u", clock->_last_rtp_timestamp, rtp_timestamp);
			}
		}

		clock->_extended_rtp_timestamp += delta;
	}

	clock->_last_rtp_timestamp = rtp_timestamp;

	// The timestamp difference can be negative.
	auto pts = clock->_pts + ((int64_t)clock->_extended_rtp_timestamp - (int64_t)clock->_extended_rtcp_timestamp);

	logtt("Calc PTS : id(%u) pts(%" PRId64 ") last_rtp_timestamp(%u) rtp_timestamp(%u) delta(%u) extended_rtp_timestamp(%" PRIu64 ")", id, pts, clock->_last_rtp_timestamp, rtp_timestamp, delta, clock->_extended_rtp_timestamp);

	return pts; 
}

bool LipSyncClock::UpdateSenderReportTime(uint32_t id, uint32_t ntp_msw, uint32_t ntp_lsw, uint32_t rtcp_timestamp)
{
	auto clock = GetClock(id);
	if(clock == nullptr)
	{
		return false;
	}

	// OBS WHIP incorrectly sends RTP Timestamp with 0xFFFFFFFF in the first SR. Below is the code to avoid this.
	if (rtcp_timestamp == 0xFFFFFFFF)
	{
		return false;
	}

	{
		std::lock_guard<std::shared_mutex> lock(_map_lock);
		_clock_enabled_map[id] = true;
	}

	std::lock_guard<std::mutex> lock(clock->_lock);

	if (clock->_first_sr == true)
	{
		clock->_extended_rtcp_timestamp = rtcp_timestamp;
		clock->_first_sr = false;
	}
	else
	{
		uint32_t delta = 0;
		if (rtcp_timestamp > clock->_last_rtcp_timestamp)
		{
			delta = rtcp_timestamp - clock->_last_rtcp_timestamp;
		}
		else
		{
			delta = clock->_last_rtcp_timestamp - rtcp_timestamp;

			if (delta > 0x80000000)
			{
				// wrap around
				delta = 0xFFFFFFFF - clock->_last_rtcp_timestamp + rtcp_timestamp + 1;
			}
			else
			{
				// reordering or duplicate or error
				delta = 0;
				logtw("RTCP timestamp is not monotonic: %u -> %u", clock->_last_rtcp_timestamp, rtcp_timestamp);
			}
		}

		clock->_extended_rtcp_timestamp += delta;
	}

	clock->_last_rtcp_timestamp = rtcp_timestamp;
	clock->_pts = ov::Converter::NtpTsToSeconds(ntp_msw, ntp_lsw) / clock->_timebase;
	clock->_updated = true;

	logtt("Update SR : id(%u) NTP(%u/%u) pts(%" PRId64 ") rtp timestamp(%u) extended timestamp (%" PRIu64 ")", 
			id, ntp_msw, ntp_lsw, (int64_t)clock->_pts, rtcp_timestamp, clock->_extended_rtcp_timestamp);

	return true;
}