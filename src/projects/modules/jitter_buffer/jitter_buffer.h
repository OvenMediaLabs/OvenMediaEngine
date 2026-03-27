//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2021 Getroot. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/mediarouter/media_buffer.h>

#define JITTER_BUFFER_TIMEBASE		1000000.0 // microseconds

// Maximum waiting time for input. 
// If the next input is not received until this waiting time has passed, 
// the jitter buffer is deactivated, and other jitter buffers are not buffered any more.
// This is used when one of the A/Vs is not input, or the initial arrival time is different due to encoding speed or other reasons.
#define MAX_INPUT_WAIT_TIME			1500000.0 // microseconds

// Buffers only up to the set value. 
// What this means is that an A/V that is out of sync beyond this will no longer try to match.
// Usually, A/V packets come within INPUT_WAIT_TIME, but when A/V differs from input, it is used up to the maximum.
#define MAX_JITTER_BUFFER_SIZE_US	2500000.0 // microseconds, 2.5 seconds
class JitterBuffer
{
public:
	JitterBuffer(uint32_t track_id, uint64_t timebase);

	// JitterBuffer timebase is 1/1,000,000
	int64_t GetTimebase();
	int64_t GetNextPtsUsec();

	int64_t GetBufferingSizeUsec();
	size_t GetBufferingSizeCount();

	bool PushMediaPacket(const std::shared_ptr<MediaPacket> &media_packet);
	std::shared_ptr<MediaPacket> PopNextMediaPacket();
	
	// If there is no input as much as MAX_JITTER_BUFFER_SIZE_US, it is judged to be inactive.
	bool IsActive();
	bool IsEmpty();
	bool IsFull();

private:
	uint32_t _track_id = 0;
	int64_t _timebase = 0;
	[[maybe_unused]] int64_t _next_pts = 0;
	ov::StopWatch _input_stop_watch;
	ov::Queue<std::shared_ptr<MediaPacket>>	_media_packet_queue;
};


// For Lip Sync
class JitterBufferDelay
{
public:
	bool CreateJitterBuffer(uint32_t track_id, int64_t timebase);
	bool PushMediaPacket(const std::shared_ptr<MediaPacket> &media_packet);
	std::shared_ptr<MediaPacket> PopNextMediaPacket();

private:
	bool DoesEveryActiveBufferHavePackets();
	std::shared_ptr<JitterBuffer> GetFullBuffer();
	std::map<uint32_t, std::shared_ptr<JitterBuffer>>	_media_packet_queue_map;
};