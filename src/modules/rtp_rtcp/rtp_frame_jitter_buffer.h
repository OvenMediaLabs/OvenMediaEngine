#pragma once

#include "base/ovlibrary/ovlibrary.h"
#include "rtp_packet.h"
#include <atomic>
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
	// Milliseconds since the frame's first packet arrived.
	uint64_t GetElapsed();
	// Milliseconds since any packet of this frame last arrived.
	uint64_t GetElapsedSinceLastPacket();

	std::shared_ptr<RtpPacket> GetFirstRtpPacket();
	std::shared_ptr<RtpPacket> GetNextRtpPacket();

	uint32_t Timestamp() { return _timestamp; }
	size_t PacketCount() { return _packets.size(); }
	bool HasStart() const { return _has_start; }
	// Set once any packet carries a keyframe mark: for H.264/H.265 that is any
	// packet with an IDR/IRAP NAL, so a lost STAP-A start does not hide it;
	// for VP8/AV1 only the first packet carries it
	bool IsKeyframe() const { return _is_keyframe; }
	uint16_t GetMarkerSequenceNumber() const { return _end_seq; }
	uint16_t GetFirstSequenceNumber() const { return _start_seq; }

	// Highest sequence number actually received in this frame (wrap-safe).
	// Used to advance the jitter buffer's "processed up to" watermark so
	// the NACK generator can drop stale pending entries without needing
	// exact frame boundaries.
	bool HasReceivedAny() const { return _has_received; }
	uint16_t GetMaxReceivedSeq() const { return _max_received_seq; }
	uint16_t GetMinReceivedSeq() const { return _min_received_seq; }

private:
	bool CheckCompleted();

	ov::StopWatch _stop_watch;
	std::chrono::steady_clock::time_point _last_packet_at;
	uint32_t _timestamp = 0;

	bool _has_start = false;
	uint16_t _start_seq = 0;
	bool _is_keyframe = false;
	bool _has_end = false;
	uint16_t _end_seq = 0;

	bool _has_received = false;
	uint16_t _max_received_seq = 0;
	uint16_t _min_received_seq = 0;

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
	// Resource bound against malformed input: half the 16-bit sequence space,
	// the most this buffer can keep in order at once (about 39 MB of RTP).
	// Real frames stay far below it, a 4K keyframe at 50 Mbps is under 4000
	// packets. Past it the incomplete head frame is given up.
	static constexpr size_t MAX_PACKETS = 32767;

	bool InsertPacket(const std::shared_ptr<RtpPacket> &packet);
	bool IsEmpty();
	// True while any frame is buffered, readable without the lock so a
	// session tick can skip idle tracks
	bool HasFrames() const { return _has_frames.load(std::memory_order_relaxed); }
	bool HasAvailableFrame();
	std::shared_ptr<RtpFrame> PopAvailableFrame();

	// An incomplete head frame is discarded once no new packet of it has
	// arrived for hold_ms_provider() milliseconds, so a frame that is still
	// streaming in is never cut short; the hold therefore ends at most one
	// hold after the frame's own arrival time. Unset hold_ms_provider keeps
	// the legacy "drop incomplete predecessor on next complete" behavior.
	void SetHoldMsProvider(std::function<uint32_t()> provider) { _hold_ms_provider = std::move(provider); }

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

	// Provider returning the first seq the NACK generator ever saw. A frame
	// without its start whose packets begin at or before it can never be
	// completed, since nothing earlier can be requested; it is given up once
	// reordering is ruled out instead of waiting out the hold. Only the
	// track's first frame can be in that position.
	void SetFirstObservedSeqProvider(std::function<std::optional<uint16_t>()> provider)
	{
		_first_observed_seq_provider = std::move(provider);
	}

	// Callback fired for every frame the buffer gives up on under the NACK
	// hold: an incomplete head discarded, or a complete head released over a
	// lost earlier frame. keyframe_arriving tells whether a keyframe, complete
	// or still arriving, is waiting in the buffer behind it, in which case the
	// picture repairs itself. Whether to request one otherwise is the owner's
	// decision.
	void SetOnFrameDiscarded(std::function<void(bool keyframe_arriving)> callback)
	{
		_on_frame_discarded = std::move(callback);
	}

private:
	using FrameMap = std::map<uint64_t, std::shared_ptr<RtpFrame>>;

	// Non-locking core shared by HasAvailableFrame() and PopAvailableFrame()
	bool HasAvailableFrameInternal() OV_REQUIRES(_lock);
	void BurnOutExpiredFrames() OV_REQUIRES(_lock);
	uint64_t GetExtentedTimestamp(uint32_t timestamp) OV_REQUIRES(_lock);
	uint32_t CurrentHoldMs() OV_REQUIRES(_lock);
	void AdvanceProcessedSeq(RtpFrame &frame) OV_REQUIRES(_lock);
	// Records bookkeeping after a frame leaves the buffer (emitted or
	// discarded): advances the processed timestamp/seq watermarks. Caller
	// still has to erase the frame from `_rtp_frames`.
	void MarkFrameProcessed(uint64_t extended_timestamp, RtpFrame &frame) OV_REQUIRES(_lock);
	void NotifyFrameDiscarded() OV_REQUIRES(_lock);
	bool IsBeyondRepair(const RtpFrame &frame) OV_REQUIRES(_lock);
	// Takes a frame out of the buffer (emitted or discarded) and returns the next iterator
	FrameMap::iterator RemoveFrame(FrameMap::iterator it) OV_REQUIRES(_lock);
	void EnforcePacketBudget() OV_REQUIRES(_lock);

	uint32_t _last_timestamp OV_GUARDED_BY(_lock) = 0;
	uint32_t _timestamp_cycle OV_GUARDED_BY(_lock) = 0;

	std::function<uint32_t()> _hold_ms_provider;
	std::function<void(uint16_t)> _on_processed_seq_advance;
	std::function<std::optional<uint16_t>()> _lowest_pending_seq_provider;
	std::function<std::optional<uint16_t>()> _first_observed_seq_provider;
	std::function<void(bool keyframe_arriving)> _on_frame_discarded;

	bool _has_processed_seq OV_GUARDED_BY(_lock) = false;
	uint16_t _last_processed_max_seq OV_GUARDED_BY(_lock) = 0;

	// Highest extended timestamp already emitted or dropped. Rejects packets
	// for an older timestamp (typical for late RTX after a frame has been
	// popped) so they don't create a phantom duplicate frame.
	bool _has_processed_timestamp OV_GUARDED_BY(_lock) = false;
	uint64_t _last_processed_timestamp OV_GUARDED_BY(_lock) = 0;

	// timestamp : RtpFrameInfo (ordered, so std::map)
	FrameMap _rtp_frames OV_GUARDED_BY(_lock);
	size_t _packet_count OV_GUARDED_BY(_lock) = 0;
	// Buffered frames carrying a keyframe mark, so a give-up answers
	// "is a keyframe already here" without scanning the buffer
	size_t _keyframe_count OV_GUARDED_BY(_lock) = 0;
	std::atomic<bool> _has_frames{false};
	bool _budget_warned OV_GUARDED_BY(_lock) = false;

	mutable ov::Mutex _lock;
};
