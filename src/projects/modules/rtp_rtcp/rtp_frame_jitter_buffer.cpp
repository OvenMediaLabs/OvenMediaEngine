#include "rtp_frame_jitter_buffer.h"

#define OV_LOG_TAG "RtpVideoJitterBuffer"

/************************************************************************
 * 								RTPFrame
 ***********************************************************************/

RtpFrame::RtpFrame(uint32_t timestamp)
{
	_timestamp = timestamp;
	_stop_watch.Start();
}

std::shared_ptr<RtpPacket> RtpFrame::GetFirstRtpPacket()
{
	if (IsCompleted() == false)
	{
		return nullptr;
	}

	_curr_order_number = _min_order_number;

	auto it = _packets.find(_curr_order_number);
	if (it == _packets.end())
	{
		return nullptr;
	}

	return it->second;
}

std::shared_ptr<RtpPacket> RtpFrame::GetNextRtpPacket()
{
	if (IsCompleted() == false)
	{
		return nullptr;
	}

	_curr_order_number ++;

	auto it = _packets.find(_curr_order_number);
	if (it == _packets.end())
	{
		return nullptr;
	}

	return it->second;
}

uint16_t RtpFrame::GetOrderNumber(uint16_t sequence_number)
{
	if (_first_packet == true)
	{
		_first_packet = false;
		_first_sequence_number = sequence_number;
		_base_order_number = std::numeric_limits<uint16_t>::max() / 2;

		return _base_order_number;
	}

	if (sequence_number > _first_sequence_number)
	{
		if (sequence_number - _first_sequence_number > (std::numeric_limits<uint16_t>::max() / 2))
		{
			// out of order : 0 -> 65535 ==> gap : -1 
			uint16_t gap = std::numeric_limits<uint16_t>::max() - sequence_number + _first_sequence_number + 1;
			return _base_order_number - gap;
		}
		else
		{
			// In order : 1 -> 2 ==> gap : +1
			return _base_order_number + (sequence_number - _first_sequence_number);
		}
	}
	else
	{
		if (_first_sequence_number - sequence_number > (std::numeric_limits<uint16_t>::max() / 2))
		{
			// Roll over : 65535 -> 0 ==> gpa : +1
			uint16_t gap = std::numeric_limits<uint16_t>::max() - _first_sequence_number + sequence_number + 1;
			return _base_order_number + gap;
		}
		else
		{
			// Out of order : 2 -> 1
			return _base_order_number - (_first_sequence_number - sequence_number);
		}
	}

	// Should not reach here
	return 0;
}

bool RtpFrame::InsertPacket(const std::shared_ptr<RtpPacket> &packet)
{
	if (packet == nullptr || packet->Timestamp() != _timestamp)
	{
		logte("Invalid packet : %s (expected ts : %u)", packet->Dump().CStr(), _timestamp);
		return false;
	}

	logtt("Insert packet : %s", packet->Dump().CStr());

	auto order_number = GetOrderNumber(packet->SequenceNumber());

	_packets.emplace(order_number, packet);

	// First packet
	_min_order_number = std::min(_min_order_number, order_number);
	_max_order_number = std::max(_max_order_number, order_number);

	if (packet->Marker())
	{
		_marked = true;
		_marker_sequence_number = packet->SequenceNumber();
	}

	// Check if frame is completed
	if (_marked == true)
	{
		CheckCompleted();
	}

	return true;
}

bool RtpFrame::IsMarked()
{
	return _marked;
}

bool RtpFrame::IsCompleted()
{
	if (_completed == true)
	{
		return true;
	}

	if (_marked == true)
	{
		return CheckCompleted();
	}

	return false;
}

bool RtpFrame::CheckCompleted()
{
	// Already completed
	if (_completed == true)
	{
		return true;
	}

	if (_marked == false)
	{
		return false;
	}

	// Check number of packets
	uint16_t need_number_of_packets = _max_order_number - _min_order_number + 1;

	// Check if frame is valid
	if (need_number_of_packets == _packets.size())
	{
		_completed = true;
		logtt("Frame completed: timestamp(%u) packets(%u) need packets(%u)", _timestamp, _packets.size(), need_number_of_packets);
	}
	else
	{
		logte("Invalid frame: timestamp(%u) %u/%u", _timestamp, _packets.size(), need_number_of_packets);
	}

	return _completed;
}

uint64_t RtpFrame::GetElapsed()
{
	return _stop_watch.Elapsed();
}

/************************************************************************
 * 							Jitter Buffer
 ***********************************************************************/

uint64_t RtpFrameJitterBuffer::GetExtentedTimestamp(uint32_t timestamp)
{
	// If the timestamp is less than the previous timestamp, it is assumed that the timestamp has been rolled over.
	if (timestamp < _last_timestamp && _last_timestamp - timestamp > 0x80000000)
	{
		_timestamp_cycle++;
	}

	_last_timestamp = timestamp;

	return (static_cast<uint64_t>(_timestamp_cycle) << 32) | timestamp;
}

bool RtpFrameJitterBuffer::InsertPacket(const std::shared_ptr<RtpPacket> &packet)
{
	auto timestamp = GetExtentedTimestamp(packet->Timestamp());

	auto it = _rtp_frames.find(timestamp);
	std::shared_ptr<RtpFrame> frame;

	if (it == _rtp_frames.end())
	{
		logtt("Create frame buffer for timestamp %llu", timestamp);
		// First packet of frame
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

void RtpFrameJitterBuffer::BurnOutExpiredFrames()
{
	// If there are completed frames among the frames, all previous frames are deleted.

	// Find first completed frame
	auto completed_frame_it = _rtp_frames.begin();
	while (completed_frame_it != _rtp_frames.end())
	{
		auto frame = completed_frame_it->second;
		if (frame->IsCompleted())
		{
			break;
		}
		++completed_frame_it;
	}

	if (completed_frame_it == _rtp_frames.begin() || completed_frame_it == _rtp_frames.end())
	{
		// No burn outted frame and completed frame
		return;
	}

	// Delete from begin to completed frame (not included)
	auto it = _rtp_frames.begin();
	while (it != completed_frame_it)
	{
		auto frame = it->second;
		logtt("Frame discarded (It may be PADDING frame for BWE) - timestamp(%u) packets(%d) marked(%s)", frame->Timestamp(), frame->PacketCount(), frame->IsMarked() ? "true" : "false");
		it = _rtp_frames.erase(it);
	}
}

bool RtpFrameJitterBuffer::HasAvailableFrame()
{
	BurnOutExpiredFrames();

	auto it = _rtp_frames.begin();
	if (it == _rtp_frames.end())
	{
		return false;
	}

	auto first_frame = it->second;
	return first_frame->IsCompleted();
}

std::shared_ptr<RtpFrame> RtpFrameJitterBuffer::PopAvailableFrame()
{
	if (HasAvailableFrame() == false)
	{
		return nullptr;
	}

	auto it = _rtp_frames.begin();
	auto frame = it->second;

	logtt("Pop frame - extended(%llu) timestamp(%u) packets(%d) frames(%u)", it->first, frame->Timestamp(), frame->PacketCount(), _rtp_frames.size());

	// remove front frame
	_rtp_frames.erase(it);

	return frame;
}