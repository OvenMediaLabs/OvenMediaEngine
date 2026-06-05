#pragma once

#include "base/ovlibrary/ovlibrary.h"
#include "rtp_packet.h"
#include <functional>
#include <optional>
#include <unordered_map>

// One frame's worth of RTP packets, identified by a common RTP timestamp.
// Completeness is decided from packet flags stamped by RtpFrameBoundaryDetector:
//   - start: IsFirstPacketOfFrame (authoritative, from DD) or IsStartOfUnit
//     (NAL/unit starts, from codec parse); the earliest such sequence number
//     is taken as the frame start.
//   - end:   IsLastPacketOfFrame (RTP marker / DD E bit).
// A frame is complete when both ends are known and every sequence number
// between start and end (inclusive, with uint16 wrap) is present.
class RtpFrame
{
public:
	RtpFrame(uint32_t timestamp);

	bool InsertPacket(const std::shared_ptr<RtpPacket> &packet);
	bool IsCompleted();
	bool IsMarked();
	uint64_t GetElapsed();

	std::shared_ptr<RtpPacket> GetFirstRtpPacket();
	std::shared_ptr<RtpPacket> GetNextRtpPacket();

	uint32_t Timestamp() { return _timestamp; }
	size_t PacketCount() { return _packets.size(); }
	bool HasStart() const { return _has_start; }
	uint16_t GetMarkerSequenceNumber() const { return _end_seq; }
	uint16_t GetFirstSequenceNumber() const { return _start_seq; }

	// Highest sequence number actually received in this frame (wrap-safe).
	// Used to advance the jitter buffer's "processed up to" watermark so
	// the NACK generator can drop stale pending entries without needing
	// exact frame boundaries.
	bool HasReceivedAny() const { return _has_received; }
	uint16_t GetMaxReceivedSeq() const { return _max_received_seq; }

private:
	bool CheckCompleted();

	ov::StopWatch _stop_watch;
	uint32_t _timestamp = 0;

	bool _has_start = false;
	uint16_t _start_seq = 0;
	bool _has_end = false;
	uint16_t _end_seq = 0;

	bool _has_received = false;
	uint16_t _max_received_seq = 0;

	bool _completed = false;
	bool _incomplete_logged = false;

	uint16_t _curr_seq = 0;  // iteration cursor for GetFirst/NextRtpPacket

	// seq : RtpPacket (wrap-safe: lookup by exact seq within [_start_seq, _end_seq] traversal).
	std::unordered_map<uint16_t, std::shared_ptr<RtpPacket>> _packets;
};

// A jitter buffer that emits frames in RTP timestamp order. Per-track
// frame boundary information must be stamped on each packet before insertion
// (see RtpFrameBoundaryDetector + RtpRtcp wiring); the buffer itself stays
// codec-agnostic.
class RtpFrameJitterBuffer
{
public:
	bool InsertPacket(const std::shared_ptr<RtpPacket> &packet);
	bool HasAvailableFrame();
	std::shared_ptr<RtpFrame> PopAvailableFrame();

	// Incomplete frames at the head are held for up to (hold_ms_provider() +
	// frame_interval_ms) milliseconds before being discarded. The frame
	// interval term covers the gap between losing a frame's last packet and
	// the next frame's first packet arriving — without it low-fps streams
	// can't NACK the lost packet in time. Unset hold_ms_provider keeps the
	// legacy "drop incomplete predecessor on next complete" behavior.
	void SetHoldMsProvider(std::function<uint32_t()> provider) { _hold_ms_provider = std::move(provider); }

	// Upper bound for the total hold (the operator's MaxHoldMs latency budget).
	// 0 = no cap. Clamps CurrentHoldMs so the frame-interval margin can't push
	// the hold past the configured ceiling.
	void SetMaxHoldMs(uint32_t max_hold_ms) { _max_hold_ms = max_hold_ms; }

	// RTP clock rate of the track. Used to convert RTP timestamp deltas
	// between consecutive frames into milliseconds for the frame interval
	// estimate. Must be set before frames start flowing.
	void SetClockRate(uint32_t clock_rate) { _clock_rate = clock_rate; }

	// Callback fired whenever the jitter buffer advances its "processed up
	// to" sequence number (after emitting or discarding a frame). The NACK
	// generator uses this to drop pending entries it no longer needs to chase.
	void SetOnProcessedSeqAdvance(std::function<void(uint16_t)> callback)
	{
		_on_processed_seq_advance = std::move(callback);
	}

	// Provider returning the lowest seq still pending NACK recovery (or
	// nullopt if none). When set, HasAvailableFrame holds a complete head
	// frame whose first packet is greater than a pending recovery, giving
	// NACK a chance to fill in the missing earlier frame.
	void SetLowestPendingSeqProvider(std::function<std::optional<uint16_t>()> provider)
	{
		_lowest_pending_seq_provider = std::move(provider);
	}

private:
	void BurnOutExpiredFrames();
	uint64_t GetExtentedTimestamp(uint32_t timestamp);
	uint32_t CurrentHoldMs();
	void UpdateFrameIntervalEstimate(uint32_t rtp_ts);
	void AdvanceProcessedSeq(RtpFrame &frame);
	// Records bookkeeping after a frame leaves the buffer (emitted or
	// discarded): advances the processed timestamp/seq watermarks and
	// updates the frame-interval estimate. Caller still has to erase the
	// frame from `_rtp_frames`.
	void MarkFrameProcessed(uint64_t extended_timestamp, RtpFrame &frame);

	uint32_t _last_timestamp = 0;
	uint32_t _timestamp_cycle = 0;

	std::function<uint32_t()> _hold_ms_provider;
	std::function<void(uint16_t)> _on_processed_seq_advance;
	std::function<std::optional<uint16_t>()> _lowest_pending_seq_provider;
	uint32_t _clock_rate = 0;
	uint32_t _max_hold_ms = 0;  // 0 = no cap

	bool _has_processed_seq = false;
	uint16_t _last_processed_max_seq = 0;

	// Conservative seed for the frame-interval mean-deviation. Until the
	// EWMA collects a few real samples we have no idea what fps / pacing
	// the publisher will use, so we err large: the first keyframe can be
	// big and pacing-bursted, and discarding it triggers a long PLI/IDR
	// recovery downstream. Real samples shrink this within a few frames.
	static constexpr uint32_t INITIAL_FRAME_INTERVAL_DEV_GUESS_MS = 50;

	// Smoothed frame interval (ms) and its mean-deviation, measured from
	// the RTP timestamp delta between consecutive processed frames.
	// CurrentHoldMs uses (mean + 4*dev) to absorb publisher pacing burst:
	// adaptive bitrate / pacer / encoder stalls can stretch a single gap
	// to several frame intervals, and a constant + dev term lets the hold
	// follow that variability without any fixed magic margin. The mean
	// starts at 0 (first sample seeds it directly) but the deviation
	// starts large so the very first frame (e.g. the initial keyframe)
	// has enough hold for NACK retries before EWMA has caught up.
	bool _has_last_processed_rtp_ts = false;
	uint32_t _last_processed_rtp_ts = 0;
	uint32_t _frame_interval_ms = 0;
	uint32_t _frame_interval_dev_ms = INITIAL_FRAME_INTERVAL_DEV_GUESS_MS;

	// Highest extended timestamp already emitted or dropped. Rejects packets
	// for an older timestamp (typical for late RTX after a frame has been
	// popped) so they don't create a phantom duplicate frame.
	bool _has_processed_timestamp = false;
	uint64_t _last_processed_timestamp = 0;

	// timestamp : RtpFrameInfo (ordered, so std::map)
	std::map<uint64_t, std::shared_ptr<RtpFrame>> _rtp_frames;
};
