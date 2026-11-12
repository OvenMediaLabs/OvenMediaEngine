#include "rtp_receive_statistics.h"

#include <algorithm>

RtpReceiveStatistics::RtpReceiveStatistics(uint32_t media_ssrc, uint32_t clock_rate, uint32_t receiver_ssrc)
{
	_media_ssrc = media_ssrc;
	_clock_rate = clock_rate;
	_receiver_ssrc = receiver_ssrc;
	if(receiver_ssrc == 0)
	{
		_receiver_ssrc = ov::Random::GenerateUInt32();
	}

	_report_block_timer.Start();
}

bool RtpReceiveStatistics::AddReceivedRtpPacket(const std::shared_ptr<RtpPacket> &packet)
{
	ov::LockGuard lock(_lock);
	if(_first == true)
	{
		_first = false;
		_media_ssrc = packet->Ssrc();
		InitSeq(packet->SequenceNumber());
	}
	else
	{
		UpdateSeq(packet->SequenceNumber());
	}

	UpdateStat(packet);

	return true;
}

bool RtpReceiveStatistics::AddReceivedRtcpSenderReport(const std::shared_ptr<SenderReport> &report)
{
	ov::LockGuard lock(_lock);
	_last_sr_received_time = std::chrono::steady_clock::now();

	// the middle 32 bits out of 64 in the NTP timestamp
	uint64_t ntp_timestamp = (uint64_t)report->GetMsw() << 32 | (uint64_t)report->GetLsw();
	_last_sr_timestamp = (ntp_timestamp >> 16) & 0xFFFFFFFF;

	return true;
}

uint32_t RtpReceiveStatistics::GetMediaSSRC()
{
	ov::LockGuard lock(_lock);
	return _media_ssrc;
}

uint32_t RtpReceiveStatistics::GetReceiverSSRC()
{
	ov::LockGuard lock(_lock);
	return _receiver_ssrc;
}

uint64_t RtpReceiveStatistics::GetNumberOfFirRequests()
{
	ov::LockGuard lock(_lock);
	return _fir_request_count;
}

void RtpReceiveStatistics::OnFirRequested()
{
	ov::LockGuard lock(_lock);
	_fir_request_count ++;
}

void RtpReceiveStatistics::InitSeq(uint16_t seq)
{
	_init_seq = seq;
	_highest_seq = seq;
	_cycles = 0;
	_received_packets = 0;
	_received_packets_prior = 0;
	_expected_packets_prior = 0;
}

// Ahead within half of the 16-bit space is newer, anything else is late or a duplicate
// No restart check as in RFC 3550 A.1, since a restarted sender comes with a new SSRC
void RtpReceiveStatistics::UpdateSeq(uint16_t seq)
{
	uint16_t distance_from_highest = static_cast<uint16_t>(seq - _highest_seq);
	bool is_newer = (distance_from_highest != 0) && (distance_from_highest < RTP_SEQ_MOD / 2);

	if (is_newer == false)
	{
		// Counted as received without moving the highest
		// A gap longer than half the space cannot be told apart from this
		return;
	}

	// Possibly after a gap, which counts as lost
	if (seq < _highest_seq)
	{
		_cycles += RTP_SEQ_MOD;
	}
	_highest_seq = seq;
}

bool RtpReceiveStatistics::UpdateStat(const std::shared_ptr<RtpPacket> &packet)
{
	// Jitter
	if (_received_packets > 0 && _last_rtp_timestamp != packet->Timestamp())
	{
		int64_t rtp_timestamp_diff = packet->Timestamp() - _last_rtp_timestamp;

		auto now = std::chrono::steady_clock::now();
		int64_t rtp_received_time_diff = std::chrono::duration_cast<std::chrono::microseconds>(now - _last_rtp_received_time).count();

		// Convert the elapsed (monotonic) time diff to RTP timestamp units
		rtp_received_time_diff = (uint32_t)((double)rtp_received_time_diff / 1000000.0 * (double)_clock_rate);

		// J(i) = J(i-1) + (|D(i-1,i)| - J(i-1))/16

		auto d = abs((int)rtp_received_time_diff - (int)rtp_timestamp_diff);

		_interarrival_jitter = _interarrival_jitter + ((d - (int)_interarrival_jitter) / 16);

		logt("DEBUG", "RTP Interarrival Jitter: %u - rtp_timestamp_diff(%" PRId64 "), rtp_received_time_diff(%" PRId64 ") d(%d)", _interarrival_jitter, rtp_timestamp_diff, rtp_received_time_diff, d);

		_last_rtp_received_time = now;
		_last_rtp_timestamp = packet->Timestamp();
	}
	else if (_received_packets == 0)
	{
		_last_rtp_received_time = std::chrono::steady_clock::now();
		_last_rtp_timestamp = packet->Timestamp();
	}

	_received_packets += 1;
	_received_bytes += packet->GetDataLength();

	return true;
}

bool RtpReceiveStatistics::HasReceivedRtpPacket()
{
	ov::LockGuard lock(_lock);
	return _first == false;
}

bool RtpReceiveStatistics::HasElapsedSinceLastReportBlock(uint32_t milliseconds)
{
	ov::LockGuard lock(_lock);
	return _report_block_timer.IsElapsed(milliseconds);
}

bool RtpReceiveStatistics::IsSenderReportReceived()
{
	ov::LockGuard lock(_lock);
	return _last_sr_timestamp != 0;
}

std::shared_ptr<ReportBlock> RtpReceiveStatistics::GenerateReportBlock()
{
	ov::LockGuard lock(_lock);

	// RFC 3550 A.3. Expected and received counts only grow, so the interval subtractions stay non-negative
	uint64_t extended_highest_seq = _cycles + _highest_seq;
	uint64_t expected_packets = extended_highest_seq - _init_seq + 1;

	int64_t expected_packets_interval = static_cast<int64_t>(expected_packets - _expected_packets_prior);
	int64_t received_packets_interval = static_cast<int64_t>(_received_packets - _received_packets_prior);
	int64_t lost_packets_interval = expected_packets_interval - received_packets_interval;
	_expected_packets_prior = expected_packets;
	_received_packets_prior = _received_packets;

	// An interval with late or duplicate packets has no loss, not a negative one
	uint8_t fraction_lost = 0;
	if (expected_packets_interval > 0 && lost_packets_interval > 0)
	{
		fraction_lost = static_cast<uint8_t>((lost_packets_interval * 256) / expected_packets_interval);
	}

	// Duplicates can make it negative; saturates at the limits of the 24-bit signed field
	int64_t lost_packets = std::clamp<int64_t>(static_cast<int64_t>(expected_packets) - static_cast<int64_t>(_received_packets), -0x800000, 0x7FFFFF);

	// Only unsigned conversions cut the values, which keep the low bits on every platform
	uint32_t cumulative_packet_lost = static_cast<uint32_t>(lost_packets) & 0xFFFFFF;
	uint32_t extended_highest_seq_field = static_cast<uint32_t>(extended_highest_seq);

	// delay since last SR (DLSR)
	uint32_t dlsr = 0;
	if (_last_sr_timestamp != 0)
	{
		// get delay since _last_sr_received_time in us
		auto delay_in_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - _last_sr_received_time).count();
		
		// convert to 1/65536 second
		dlsr = (uint32_t)((double)delay_in_us / 1000000.0 * 65536.0);
	}
	
	_report_block_timer.Update();

	auto report_block = std::make_shared<ReportBlock>(_media_ssrc, fraction_lost, cumulative_packet_lost, extended_highest_seq_field, _interarrival_jitter, _last_sr_timestamp, dlsr);

	// Reset interval values
	_last_sr_timestamp = 0;
	
	return report_block;
}

