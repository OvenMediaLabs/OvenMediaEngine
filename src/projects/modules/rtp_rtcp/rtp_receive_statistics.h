#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "rtp_packet.h"
#include "rtcp_info/sender_report.h"

#define RTP_SEQ_MOD	(1<<16)
#define MAX_DROPOUT	3000
#define	MAX_MISORDER 100

class RtpReceiveStatistics
{
public:
	// There is no ssrc if the receiver does not transmit a packet.
	// So if receiver_ssrc is 0, it will be generated randomly. It can be used to create RR packet
	RtpReceiveStatistics(uint32_t media_ssrc, uint32_t clock_rate, uint32_t receiver_ssrc = 0);

	bool AddReceivedRtpPacket(const std::shared_ptr<RtpPacket> &packet);
	bool AddReceivedRtcpSenderReport(const std::shared_ptr<SenderReport> &report);

	bool HasElapsedSinceLastReportBlock(uint32_t milliseconds);
	bool IsSenderReportReceived();

	std::shared_ptr<ReportBlock> GenerateReportBlock();

	uint32_t GetMediaSSRC();
	uint32_t GetReceiverSSRC();

	uint64_t GetNumberOfFirRequests();
	void OnFirRequested();

	void InitSeq(uint16_t seq);
	bool UpdateSeq(uint16_t seq);
	bool UpdateStat(const std::shared_ptr<RtpPacket> &packet);

private:
	uint32_t	_media_ssrc = 0;
	uint32_t	_receiver_ssrc = 0;		// random generated local ssrc, for ReceiverReport, FIR ... 
	uint32_t	_clock_rate = 0;

	// From SR
	std::chrono::system_clock::time_point _last_sr_received_time;
	uint32_t	_last_sr_timestamp = 0;

	// For Receiver Report
	bool		_first = true;
	
	// https://tools.ietf.org/html/rfc3550#page-75
	uint16_t	_highest_seq = 0;
	uint32_t	_cycles = 0;
	uint32_t	_init_seq = 0;
	uint32_t	_received_packets = 0;
	uint32_t	_expected_packets_prior = 0;
	uint32_t	_received_packets_prior = 0;
	uint32_t	_interarrival_jitter = 0;

	uint32_t 	_last_rtp_timestamp = 0;
	std::chrono::system_clock::time_point _last_rtp_received_time;

	uint32_t	_received_bytes = 0;

	uint64_t	_fir_request_count = 0;

	ov::StopWatch	_report_block_timer;
};