#pragma once

#include "base/common_types.h"
#include "../rtp_packet.h"
#include "../rtcp_packet.h"
#include "sender_report.h"

class RtcpReportBlockGenerator
{
public:
	RtcpReportBlockGenerator(uint32_t ssrc, uint32_t codec_rate);

	void AddRTPPacketInfo(const std::shared_ptr<RtpPacket> &rtp_packet);
	void AddSenderReportInfo(const std::shared_ptr<SenderReport> &sender_report);

	bool IsAvailableRtcpRRPacket() const;
	std::shared_ptr<RtcpPacket> PopRtcpRRPacket();

private:
	uint32_t GetExtendedHighestSequenceNumber();

	bool _initialized = false;
	bool _session_initialized = false;

	uint32_t _ssrc = 0;
	uint32_t _codec_rate = 1;

	uint32_t _initial_sequence_number = 0;
	uint32_t _session_initial_sequence_number = 0;

	uint32_t _sequence_number_cycle = 0;
	uint32_t _last_sequence_number = 0;
	
	uint32_t _packet_count = 0;
	uint32_t _session_packet_count = 0;

	uint32_t _interarrival_jitter = 0;
	uint32_t _last_rtp_timestamp = 0;
	std::chrono::system_clock::time_point _last_rtp_received_time;

	uint32_t _last_sender_report_timestamp = 0;
	std::chrono::system_clock::time_point _last_sender_report_received_time;
};