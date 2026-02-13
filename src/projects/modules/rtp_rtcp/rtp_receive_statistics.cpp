#include "rtp_receive_statistics.h"

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
	_last_sr_received_time = std::chrono::system_clock::now();

	// the middle 32 bits out of 64 in the NTP timestamp
	uint64_t ntp_timestamp = (uint64_t)report->GetMsw() << 32 | (uint64_t)report->GetLsw();
	_last_sr_timestamp = (ntp_timestamp >> 16) & 0xFFFFFFFF;

	return true;
}

uint32_t RtpReceiveStatistics::GetMediaSSRC()
{
	return _media_ssrc;
}

uint32_t RtpReceiveStatistics::GetReceiverSSRC()
{
	return _receiver_ssrc;
}

uint64_t RtpReceiveStatistics::GetNumberOfFirRequests()
{
	return _fir_request_count;
}

void RtpReceiveStatistics::OnFirRequested()
{
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

bool RtpReceiveStatistics::UpdateSeq(uint16_t seq)
{
	// Wrapped
	if (seq < _highest_seq && _highest_seq - seq > RTP_SEQ_MOD / 2)
	{
		_cycles += RTP_SEQ_MOD;
	}
	
	// TODO(Getroot): Check "large jump", "probation" and "reordered/duplicate"
	_highest_seq = seq;

	//logi("DEBUG", "RTP: UpdateSeq - extended seq(%u) cycles(%u), highest_seq(%u)", _cycles + _highest_seq, _cycles, _highest_seq);

	return true;
}

bool RtpReceiveStatistics::UpdateStat(const std::shared_ptr<RtpPacket> &packet)
{
	// Jitter
	if (_received_packets > 0 && _last_rtp_timestamp != packet->Timestamp())
	{
		int64_t rtp_timestamp_diff = packet->Timestamp() - _last_rtp_timestamp;

		auto now = std::chrono::system_clock::now();
		int64_t rtp_received_time_diff = std::chrono::duration_cast<std::chrono::microseconds>(now - _last_rtp_received_time).count();

		// Calculate wall clock diff in RTP timestamp units
		rtp_received_time_diff = (uint32_t)((double)rtp_received_time_diff / 1000000.0 * (double)_clock_rate);

		// J(i) = J(i-1) + (|D(i-1,i)| - J(i-1))/16

		auto d = abs((int)rtp_received_time_diff - (int)rtp_timestamp_diff);

		_interarrival_jitter = _interarrival_jitter + ((d - (int)_interarrival_jitter) / 16);

		logt("DEBUG", "RTP Interarrival Jitter: %u - rtp_timestamp_diff(%lld), rtp_received_time_diff(%lld) d(%d)", _interarrival_jitter, rtp_timestamp_diff, rtp_received_time_diff, d);

		_last_rtp_received_time = now;
		_last_rtp_timestamp = packet->Timestamp();
	}
	else if (_received_packets == 0)
	{
		_last_rtp_received_time = std::chrono::system_clock::now();
		_last_rtp_timestamp = packet->Timestamp();
	}

	_received_packets += 1;
	_received_bytes += packet->GetDataLength();

	return true;
}

bool RtpReceiveStatistics::HasElapsedSinceLastReportBlock(uint32_t milliseconds)
{
	return _report_block_timer.IsElapsed(milliseconds);
}

bool RtpReceiveStatistics::IsSenderReportReceived()
{
	return _last_sr_timestamp != 0;
}

std::shared_ptr<ReportBlock> RtpReceiveStatistics::GenerateReportBlock()
{
	int32_t extented_highest_seq = _cycles + _highest_seq;
	int32_t expected_packet_count = extented_highest_seq - _init_seq + 1;

	int32_t expected_packet_count_interval = expected_packet_count - _expected_packets_prior;
	_expected_packets_prior = expected_packet_count;
	int32_t received_packet_count_interval = _received_packets - _received_packets_prior;
	_received_packets_prior = _received_packets;

	int32_t packet_lost_interval = expected_packet_count_interval - received_packet_count_interval;

	// fraction lost
	int8_t fraction_lost = ((double)packet_lost_interval / (double)expected_packet_count_interval) * 256.0;

	// cumulative lost
	int32_t packet_lost = expected_packet_count - _received_packets;
	uint32_t cumulative_packet_lost = 0;
	if (packet_lost < 0)
	{
		cumulative_packet_lost = 0x800000 | (packet_lost & 0x7FFFFF);
	}
	else
	{
		cumulative_packet_lost = packet_lost & 0x7FFFFF;
	}

	// delay since last SR (DLSR)
	uint32_t dlsr = 0;
	if (_last_sr_timestamp != 0)
	{
		// get delay since _last_sr_received_time in us
		auto delay_in_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - _last_sr_received_time).count();
		
		// convert to 1/65536 second
		dlsr = (uint32_t)((double)delay_in_us / 1000000.0 * 65536.0);
	}
	
	_report_block_timer.Update();

	auto report_block = std::make_shared<ReportBlock>(_media_ssrc, fraction_lost, cumulative_packet_lost, extented_highest_seq, _interarrival_jitter, _last_sr_timestamp, dlsr);

	// Reset interval values
	_last_sr_timestamp = 0;
	
	return report_block;
}

