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

	// Only packets of the media stream itself, not ones that arrived through RTX.
	// Returns false if the packet is not counted (a sequence jump not yet confirmed)
	bool AddReceivedRtpPacket(const std::shared_ptr<RtpPacket> &packet);
	bool AddReceivedRtcpSenderReport(const std::shared_ptr<SenderReport> &report);

	bool HasElapsedSinceLastReportBlock(uint32_t milliseconds);
	bool IsSenderReportReceived();

	std::shared_ptr<ReportBlock> GenerateReportBlock();

	uint32_t GetMediaSSRC();
	uint32_t GetReceiverSSRC();

	uint64_t GetNumberOfFirRequests();
	void OnFirRequested();

private:
	void InitSeq(uint16_t seq) OV_REQUIRES(_lock);
	// Returns false while a large sequence jump waits for the next packet to confirm it
	bool UpdateSeq(uint16_t seq) OV_REQUIRES(_lock);
	bool UpdateStat(const std::shared_ptr<RtpPacket> &packet) OV_REQUIRES(_lock);
	uint32_t	_media_ssrc OV_GUARDED_BY(_lock) = 0;
	uint32_t	_receiver_ssrc = 0;		// random generated local ssrc, for ReceiverReport, FIR ...
	uint32_t	_clock_rate = 0;

	// From SR
	std::chrono::steady_clock::time_point _last_sr_received_time OV_GUARDED_BY(_lock);
	uint32_t	_last_sr_timestamp OV_GUARDED_BY(_lock) = 0;

	// For Receiver Report
	bool		_first OV_GUARDED_BY(_lock) = true;

	// https://tools.ietf.org/html/rfc3550#page-75
	// Counts are 64-bit so they never wrap; the report fields are cut to their width when written
	uint16_t	_highest_seq OV_GUARDED_BY(_lock) = 0;
	uint64_t	_cycles OV_GUARDED_BY(_lock) = 0;
	uint32_t	_init_seq OV_GUARDED_BY(_lock) = 0;
	// Sequence number that would confirm a large jump, out of the 16-bit range when none is pending
	uint32_t	_bad_seq OV_GUARDED_BY(_lock) = RTP_SEQ_MOD + 1;
	uint64_t	_received_packets OV_GUARDED_BY(_lock) = 0;
	uint64_t	_expected_packets_prior OV_GUARDED_BY(_lock) = 0;
	uint64_t	_received_packets_prior OV_GUARDED_BY(_lock) = 0;
	uint32_t	_interarrival_jitter OV_GUARDED_BY(_lock) = 0;

	uint32_t 	_last_rtp_timestamp OV_GUARDED_BY(_lock) = 0;
	std::chrono::steady_clock::time_point _last_rtp_received_time OV_GUARDED_BY(_lock);

	uint32_t	_received_bytes OV_GUARDED_BY(_lock) = 0;

	uint64_t	_fir_request_count OV_GUARDED_BY(_lock) = 0;

	ov::StopWatch	_report_block_timer OV_GUARDED_BY(_lock);

	// Locked at the public methods; InitSeq/UpdateSeq/UpdateStat run under it
	mutable ov::Mutex _lock;
};