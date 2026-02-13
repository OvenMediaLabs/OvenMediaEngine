#include "lip_sync_clock.h"
#include "base/ovlibrary/clock.h"
#define OV_LOG_TAG "LipSyncClock"

bool LipSyncClock::RegisterRtpClock(uint32_t id, double timebase)
{
	auto clock = std::make_shared<Clock>();
	clock->_timebase = timebase;
	_clock_map.emplace(id, clock);

	return true;
}

std::shared_ptr<LipSyncClock::Clock> LipSyncClock::GetClock(uint32_t id)
{
	if(_clock_map.find(id) == _clock_map.end())
	{
		return nullptr;
	}
	return _clock_map[id];
}

std::optional<uint64_t> LipSyncClock::CalcPTS(uint32_t id, uint32_t rtp_timestamp)
{
	auto clock = GetClock(id);
	if(clock == nullptr)
	{
		return {};
	}

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

	std::shared_lock<std::shared_mutex> lock(clock->_clock_lock);
	// The timestamp difference can be negative.
	auto pts = clock->_pts + ((int64_t)clock->_extended_rtp_timestamp - (int64_t)clock->_extended_rtcp_timestamp);

	// This is to make pts start at zero.
	if (_first_pts == true)
	{
		// pts in 100/10000000
		_adjust_pts_us = (double)pts * clock->_timebase * 100000.0;
		_first_pts = false;
	}

	int64_t final_pts = pts - (int64_t)(_adjust_pts_us / clock->_timebase / 100000.0);

	logtt("Calc PTS : id(%u) pts(%lld) final_pts(%lld) last_rtp_timestamp(%u) rtp_timestamp(%u) delta(%u) extended_rtp_timestamp(%llu)", id, pts, final_pts, clock->_last_rtp_timestamp, rtp_timestamp, delta, clock->_extended_rtp_timestamp);

	return final_pts; 
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

	_enabled = true;

	std::lock_guard<std::shared_mutex> lock(clock->_clock_lock);
	clock->_updated = true;

	if (_first_sr == true)
	{
		clock->_extended_rtcp_timestamp = rtcp_timestamp;
		_first_sr = false;
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

	logtt("Update SR : id(%u) NTP(%u/%u) pts(%lld) rtp timestamp(%u) extended timestamp (%llu)", 
			id, ntp_msw, ntp_lsw, clock->_pts, clock->_last_rtcp_timestamp, clock->_extended_rtcp_timestamp);

	return true;
}