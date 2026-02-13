#include "rtcp_report_block_generator.h"
#include "report_block.h"

RtcpReportBlockGenerator::RtcpReportBlockGenerator(uint32_t ssrc, uint32_t codec_rate)
{
	_ssrc = ssrc;
	_codec_rate = codec_rate;
}

void RtcpReportBlockGenerator::AddRTPPacketInfo(const std::shared_ptr<RtpPacket> &rtp_packet)
{
	if (_initialized == false)
	{
		_initial_sequence_number = rtp_packet->SequenceNumber();
		_initialized = true;
	}

	if (_session_initialized == false)
	{
		_session_initial_sequence_number = rtp_packet->SequenceNumber();
		_session_initialized = true;
	}
	
	if (_packet_count > 0)
	{
		// Calculate interarrival jitter
		uint32_t rtp_timestamp_diff = rtp_packet->Timestamp() - _last_rtp_timestamp;

		// Get wall clock time in milliseconds
		auto clock_timestamp_diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - _last_rtp_received_time).count();

		// Calculate clock_timestamp_diff in RTP timestamp units
		clock_timestamp_diff = (clock_timestamp_diff * _codec_rate) / 1000;

		// J(i) = J(i-1) + (|D(i-1,i)| - J(i-1))/16
		_interarrival_jitter = _interarrival_jitter + ((abs((int)rtp_timestamp_diff - (int)clock_timestamp_diff) - _interarrival_jitter) / 16);

		// Calculate sequence number cycle
		if (rtp_packet->SequenceNumber() < _last_sequence_number)
		{
			_sequence_number_cycle += 1;
		}
	}

	_last_sequence_number = rtp_packet->SequenceNumber();
	_last_rtp_timestamp = rtp_packet->Timestamp();
	_last_rtp_received_time = std::chrono::system_clock::now();

	_packet_count += 1;
	_session_packet_count += 1;
}

void RtcpReportBlockGenerator::AddSenderReportInfo(const std::shared_ptr<SenderReport> &sender_report)
{
	// Make 64bits NTP timestamp
	uint64_t ntp_timestamp = (uint64_t)sender_report->GetMsw() << 32 | sender_report->GetLsw();

	// Convert to LSR(Last SR Timestamp) that the middle 32 bits out of 64 in the NTP timestamp
	uint32_t lsr = (ntp_timestamp >> 16) & 0xFFFFFFFF;
	
	_last_sender_report_timestamp = lsr;

	_last_sender_report_received_time = std::chrono::system_clock::now();
}

bool RtcpReportBlockGenerator::IsAvailableRtcpRRPacket() const
{
	return _session_packet_count > 0;
}

std::shared_ptr<RtcpPacket> RtcpReportBlockGenerator::PopRtcpRRPacket()
{
	if (_session_packet_count == 0)
	{
		return nullptr;
	}

	// fraction lost
	uint32_t expected_session_packet_count = GetExtendedHighestSequenceNumber() - _session_initial_sequence_number + 1;
	uint32_t session_lost = expected_session_packet_count - _session_packet_count;
	uint8_t fraction_lost = (session_lost / expected_session_packet_count) * 256;

	// cumulative number of packets lost in 24bits
	int32_t expected_packet_count = GetExtendedHighestSequenceNumber() - _initial_sequence_number + 1;
	int32_t packet_lost = expected_packet_count - _packet_count;
	uint32_t cumulative_packet_lost = 0;
	if (packet_lost < 0)
	{
		cumulative_packet_lost = 0x800000 | (packet_lost & 0x7FFFFF);
	}
	else
	{
		cumulative_packet_lost = packet_lost & 0x7FFFFF;
	}

	// extended highest sequence number
	uint32_t extended_highest_sequence_number = GetExtendedHighestSequenceNumber();

	// interarrival jitter
	uint32_t interarrival_jitter = _interarrival_jitter;

	// last SR timestamp
	uint32_t last_sender_report_timestamp = _last_sender_report_timestamp;

	// delay since last SR
	uint32_t delay_since_last_sender_report = 0;
	if (last_sender_report_timestamp != 0)
	{
		// Get wall clock time in milliseconds
		auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - _last_sender_report_received_time).count();

		// Convert to delay since last SR in 65536 units
		delay_since_last_sender_report = (delay * 65536) / 1000;
	}

	// Create RTCP RR packet
	auto report_block = std::make_shared<ReportBlock>(_ssrc, fraction_lost, cumulative_packet_lost, extended_highest_sequence_number, interarrival_jitter, last_sender_report_timestamp, delay_since_last_sender_report);

	// Initialize 
	_session_initialized = false;
	_session_packet_count = 0;
	_last_sender_report_timestamp = 0;

	return nullptr;
}

uint32_t RtcpReportBlockGenerator::GetExtendedHighestSequenceNumber()
{
	return _sequence_number_cycle * 65536 + _last_sequence_number;
}