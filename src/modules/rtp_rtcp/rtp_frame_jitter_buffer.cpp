#include "rtp_frame_jitter_buffer.h"

#define OV_LOG_TAG "RtpVideoJitterBuffer"

/************************************************************************
 *                               RtpFrame
 ***********************************************************************/

RtpFrame::RtpFrame(uint32_t timestamp)
{
	_timestamp = timestamp;
	_stop_watch.Start();
	_last_packet_at = std::chrono::steady_clock::now();
}

bool RtpFrame::InsertPacket(const std::shared_ptr<RtpPacket> &packet)
{
	if (packet == nullptr || packet->Timestamp() != _timestamp)
	{
		logte("Invalid packet : %s (expected ts : %u)",
			  packet != nullptr ? packet->Dump().CStr() : "null", _timestamp);
		return false;
	}

	logtt("Insert packet : %s", packet->Dump().CStr());

	// Frame start: DD marks it authoritatively (IsFirstPacketOfFrame, one per
	// frame); without DD the codec parse only marks NAL/unit starts
	// (IsStartOfUnit), which a multi-NAL access unit sets on several packets.
	// Either way, keep the earliest seq as the start (wrap-safe) so it is the
	// true frame start, not the last NAL to arrive.
	if (packet->IsFirstPacketOfFrame() || packet->IsStartOfUnit())
	{
		if (_has_start == false || static_cast<int16_t>(packet->SequenceNumber() - _start_seq) < 0)
		{
			_start_seq = packet->SequenceNumber();
			_has_start = true;
		}
	}
	if (packet->IsLastPacketOfFrame())
	{
		_end_seq = packet->SequenceNumber();
		_has_end = true;
	}
	if (packet->IsKeyframe())
	{
		_is_keyframe = true;
	}

	// Only a packet not seen before counts as progress; a duplicate copy must
	// not keep an incomplete frame alive.
	if (_packets.emplace(packet->SequenceNumber(), packet).second)
	{
		_last_packet_at = std::chrono::steady_clock::now();
	}

	auto seq = packet->SequenceNumber();
	if (_has_received == false || static_cast<int16_t>(seq - _max_received_seq) > 0)
	{
		_max_received_seq = seq;
		_has_received = true;
	}

	if (_has_start && _has_end)
	{
		CheckCompleted();
	}

	return true;
}

bool RtpFrame::IsMarked()
{
	return _has_end;
}

bool RtpFrame::IsCompleted()
{
	if (_completed)
	{
		return true;
	}
	if (_has_start && _has_end)
	{
		return CheckCompleted();
	}
	return false;
}

bool RtpFrame::CheckCompleted()
{
	if (_completed)
	{
		return true;
	}
	if (_has_start == false || _has_end == false)
	{
		return false;
	}

	// uint16 subtraction handles seq wrap (e.g. start=65530, end=5 → 12).
	uint16_t expected = static_cast<uint16_t>(_end_seq - _start_seq + 1);
	if (_packets.size() == expected)
	{
		_completed = true;
		logtt("Frame completed: ts(%u) start_seq(%u) end_seq(%u) packets(%zu)",
			  _timestamp, _start_seq, _end_seq, _packets.size());
		return true;
	}

	if (_incomplete_logged == false)
	{
		_incomplete_logged = true;
		logtd("Incomplete frame: ts(%u) start_seq(%u) end_seq(%u) %zu/%u",
			  _timestamp, _start_seq, _end_seq, _packets.size(), expected);
	}
	return false;
}

uint64_t RtpFrame::GetElapsed()
{
	return _stop_watch.Elapsed();
}

uint64_t RtpFrame::GetElapsedSinceLastPacket()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::steady_clock::now() - _last_packet_at)
		.count();
}

std::shared_ptr<RtpPacket> RtpFrame::GetFirstRtpPacket()
{
	if (IsCompleted() == false)
	{
		return nullptr;
	}
	_curr_seq = _start_seq;
	auto it = _packets.find(_curr_seq);
	return (it != _packets.end()) ? it->second : nullptr;
}

std::shared_ptr<RtpPacket> RtpFrame::GetNextRtpPacket()
{
	if (IsCompleted() == false)
	{
		return nullptr;
	}
	if (_curr_seq == _end_seq)
	{
		return nullptr;
	}
	_curr_seq = static_cast<uint16_t>(_curr_seq + 1);
	auto it = _packets.find(_curr_seq);
	return (it != _packets.end()) ? it->second : nullptr;
}

/************************************************************************
 *                          RtpFrameJitterBuffer
 ***********************************************************************/

uint64_t RtpFrameJitterBuffer::GetExtentedTimestamp(uint32_t timestamp)
{
	if (timestamp < _last_timestamp && _last_timestamp - timestamp > 0x80000000)
	{
		_timestamp_cycle++;
	}
	_last_timestamp = timestamp;
	return (static_cast<uint64_t>(_timestamp_cycle) << 32) | timestamp;
}

uint32_t RtpFrameJitterBuffer::CurrentHoldMs()
{
	return _hold_ms_provider ? _hold_ms_provider() : 0;
}

bool RtpFrameJitterBuffer::HasKeyframeInBuffer()
{
	for (const auto &[timestamp, frame] : _rtp_frames)
	{
		if (frame->IsKeyframe())
		{
			return true;
		}
	}
	return false;
}

void RtpFrameJitterBuffer::NotifyFrameDiscarded()
{
	if (_on_frame_discarded != nullptr)
	{
		_on_frame_discarded(HasKeyframeInBuffer());
	}
}

bool RtpFrameJitterBuffer::InsertPacket(const std::shared_ptr<RtpPacket> &packet)
{
	ov::LockGuard<ov::Mutex> lock(_lock);
	auto timestamp = GetExtentedTimestamp(packet->Timestamp());

	// Reject packets for an already-emitted/dropped frame. Without this a
	// late RTX would resurrect a phantom RtpFrame for an old timestamp.
	if (_has_processed_timestamp && timestamp <= _last_processed_timestamp)
	{
		return false;
	}

	auto it = _rtp_frames.find(timestamp);
	std::shared_ptr<RtpFrame> frame;
	if (it == _rtp_frames.end())
	{
		frame = std::make_shared<RtpFrame>(packet->Timestamp());
		_rtp_frames[timestamp] = frame;
	}
	else
	{
		frame = it->second;
	}

	frame->InsertPacket(packet);
	return true;
}

void RtpFrameJitterBuffer::MarkFrameProcessed(uint64_t extended_timestamp, RtpFrame &frame)
{
	_last_processed_timestamp = extended_timestamp;
	_has_processed_timestamp = true;
	AdvanceProcessedSeq(frame);
}

void RtpFrameJitterBuffer::AdvanceProcessedSeq(RtpFrame &frame)
{
	if (frame.HasReceivedAny() == false)
	{
		return;
	}
	auto seq = frame.GetMaxReceivedSeq();
	if (_has_processed_seq == false || static_cast<int16_t>(seq - _last_processed_max_seq) > 0)
	{
		logtt("Advance processed seq: ts(%u) max_recv_seq(%u) prev(%u) packets(%zu) completed(%s)",
			  frame.Timestamp(), seq, _last_processed_max_seq, frame.PacketCount(),
			  frame.IsCompleted() ? "true" : "false");
		_last_processed_max_seq = seq;
		_has_processed_seq = true;
		if (_on_processed_seq_advance)
		{
			_on_processed_seq_advance(seq);
		}
	}
}

void RtpFrameJitterBuffer::BurnOutExpiredFrames()
{
	// Legacy path when no NACK hold is wired in: drop incomplete predecessors
	// only when a later completed frame exists. Matches pre-NACK behavior so
	// a single in-flight frame isn't burned out prematurely.
	if (!_hold_ms_provider)
	{
		auto completed_it = _rtp_frames.begin();
		while (completed_it != _rtp_frames.end() && completed_it->second->IsCompleted() == false)
		{
			++completed_it;
		}
		if (completed_it == _rtp_frames.begin() || completed_it == _rtp_frames.end())
		{
			return;
		}

		auto it = _rtp_frames.begin();
		while (it != completed_it)
		{
			auto frame = it->second;
			logtt("Frame discarded - ts(%u) packets(%zu) marked(%s)",
				  frame->Timestamp(), frame->PacketCount(), frame->IsMarked() ? "true" : "false");
			MarkFrameProcessed(it->first, *frame);
			it = _rtp_frames.erase(it);
		}
		return;
	}

	// NACK-aware path: hold an incomplete head frame while it is still
	// progressing; drop it once no packet of it arrived for hold_ms so
	// subsequent completed frames can flow.
	uint32_t hold_ms = CurrentHoldMs();
	auto it = _rtp_frames.begin();
	while (it != _rtp_frames.end())
	{
		auto frame = it->second;
		if (frame->IsCompleted())
		{
			break;
		}
		// The hold clock runs from the frame's last new packet: a large keyframe
		// crawling over a slow uplink keeps resetting it, so only a frame that
		// stopped progressing is discarded.
		if (frame->GetElapsedSinceLastPacket() <= hold_ms)
		{
			break;
		}
		// Frame dropped after exhausting the NACK hold; log its extent so
		// recovery failures can be traced.
		logtd("Frame discarded after NACK hold %ums - ts(%u) packets(%zu) marked(%s) "
			  "has_start(%s) start_seq(%u) has_end(%s) end_seq(%u) max_recv_seq(%u) elapsed(%llums) since_last(%llums)",
			  hold_ms, frame->Timestamp(), frame->PacketCount(), frame->IsMarked() ? "true" : "false",
			  frame->HasStart() ? "true" : "false",
			  frame->GetFirstSequenceNumber(),
			  frame->IsMarked() ? "true" : "false",
			  frame->GetMarkerSequenceNumber(),
			  frame->GetMaxReceivedSeq(),
			  static_cast<unsigned long long>(frame->GetElapsed()),
			  static_cast<unsigned long long>(frame->GetElapsedSinceLastPacket()));
		MarkFrameProcessed(it->first, *frame);
		it = _rtp_frames.erase(it);
		NotifyFrameDiscarded();
	}
}

bool RtpFrameJitterBuffer::HasAvailableFrame()
{
	ov::LockGuard<ov::Mutex> lock(_lock);
	return HasAvailableFrameInternal();
}

bool RtpFrameJitterBuffer::HasAvailableFrameInternal()
{
	BurnOutExpiredFrames();
	auto it = _rtp_frames.begin();
	if (it == _rtp_frames.end())
	{
		return false;
	}
	auto head = it->second;
	if (head->IsCompleted() == false)
	{
		return false;
	}

	// Head is complete, but if NACK is still recovering an earlier seq
	// (a packet from a frame whose object never materialized because all
	// of its packets were lost), hold this frame until NACK either
	// succeeds or the hold window expires. Without this, a single-packet
	// frame can race past the missing prior frame and emit out-of-order.
	if (_lowest_pending_seq_provider)
	{
		auto lowest = _lowest_pending_seq_provider();
		if (lowest.has_value())
		{
			auto head_start = head->GetFirstSequenceNumber();
			// wrap-safe: lowest < head_start ?
			if (static_cast<int16_t>(*lowest - head_start) < 0)
			{
				uint32_t hold_ms = CurrentHoldMs();
				if (head->GetElapsed() <= hold_ms)
				{
					logtt("Hold complete head for prior NACK recovery: ts(%u) head_start(%u) lowest_pending(%u) elapsed(%llums) hold(%ums)",
						  head->Timestamp(), head_start, *lowest,
						  static_cast<unsigned long long>(head->GetElapsed()), hold_ms);
					return false;
				}
				logtd("Release held head despite prior NACK pending (hold expired): ts(%u) head_start(%u) lowest_pending(%u) elapsed(%llums) hold(%ums)",
					  head->Timestamp(), head_start, *lowest,
					  static_cast<unsigned long long>(head->GetElapsed()), hold_ms);
				NotifyFrameDiscarded();
			}
		}
	}

	return true;
}

std::shared_ptr<RtpFrame> RtpFrameJitterBuffer::PopAvailableFrame()
{
	ov::LockGuard<ov::Mutex> lock(_lock);
	if (HasAvailableFrameInternal() == false)
	{
		return nullptr;
	}

	auto it = _rtp_frames.begin();
	auto frame = it->second;

	logtt("Pop frame ts(%u) ext(%" PRIu64 ") packets(%zu) elapsed(%llums) buffered(%zu)",
		  frame->Timestamp(), it->first, frame->PacketCount(),
		  static_cast<unsigned long long>(frame->GetElapsed()),
		  _rtp_frames.size());

	MarkFrameProcessed(it->first, *frame);
	_rtp_frames.erase(it);
	return frame;
}
