#include "jitter_buffer.h"

JitterBuffer::JitterBuffer(uint32_t track_id, uint64_t timebase)
	: _media_packet_queue("WebRTCJitterBuffer", 1000)
{
	_timebase = timebase;
	_track_id = track_id;
	_input_stop_watch.Start();
}

int64_t JitterBuffer::GetTimebase()
{
	return _timebase;
}

// JitterBuffer timebase is 1/1,000,000
int64_t JitterBuffer::GetNextPtsUsec()
{
	auto data = _media_packet_queue.Front(0);
	if(data.has_value() == false)
	{
		return -1;
	}

	auto origin_pts = data.value()->GetPts();
	auto next_pts = ((double)origin_pts / (double)_timebase) * JITTER_BUFFER_TIMEBASE;

	return next_pts;
}

bool JitterBuffer::PushMediaPacket(const std::shared_ptr<MediaPacket> &media_packet)
{
	_input_stop_watch.Update();
	_media_packet_queue.Enqueue(media_packet);
	return true;
}

std::shared_ptr<MediaPacket> JitterBuffer::PopNextMediaPacket()
{
	auto data = _media_packet_queue.Dequeue(0);
	if(data.has_value())
	{
		return data.value();
	}

	return nullptr;
}

size_t JitterBuffer::GetBufferingSizeCount()
{
	return _media_packet_queue.Size();
}

bool JitterBuffer::IsEmpty()
{
	return _media_packet_queue.IsEmpty();
}

bool JitterBuffer::IsActive()
{
	return _input_stop_watch.IsElapsed(MAX_JITTER_BUFFER_SIZE_US / 1000) == false;
}

int64_t JitterBuffer::GetBufferingSizeUsec()
{
	if(_media_packet_queue.Size() <= 1)
	{
		return 0;
	}

	auto front = _media_packet_queue.Front(0);
	auto back = _media_packet_queue.Back(0);
	auto front_pts = ((double)front.value()->GetPts() / (double)_timebase) * JITTER_BUFFER_TIMEBASE;
	auto back_pts = ((double)back.value()->GetPts() / (double)_timebase) * JITTER_BUFFER_TIMEBASE;

	return (uint64_t)(back_pts - front_pts);
}

bool JitterBuffer::IsFull()
{
	return GetBufferingSizeUsec() >= MAX_JITTER_BUFFER_SIZE_US;
}

bool JitterBufferDelay::CreateJitterBuffer(uint32_t track_id, int64_t timebase)
{
	_media_packet_queue_map.emplace(track_id, std::make_shared<JitterBuffer>(track_id, timebase));
	return true;
}

bool JitterBufferDelay::PushMediaPacket(const std::shared_ptr<MediaPacket> &media_packet)
{
	// TODO : Set max size
	auto it = _media_packet_queue_map.find(media_packet->GetTrackId());
	if(it == _media_packet_queue_map.end())
	{
		return false;
	}

	return it->second->PushMediaPacket(media_packet);
}

std::shared_ptr<MediaPacket> JitterBufferDelay::PopNextMediaPacket()
{
	if(DoesEveryActiveBufferHavePackets() == false)
	{
		// If there are a empty buffer and a full buffer, pop from the full buffer
		// Unlike the initial negotiations, either video or audio is not coming in at all.
		auto buffer = GetFullBuffer();
		if(buffer == nullptr)
		{
			return nullptr;
		}

		logt("DEBUG", "Full buffer (%d)", buffer->GetBufferingSizeCount());

		return buffer->PopNextMediaPacket();
	}

	// Pop lowest PTS first
	std::shared_ptr<JitterBuffer> next_jitter_buffer = nullptr;
	for(const auto &item : _media_packet_queue_map)
	{
		auto buffer = item.second;

		logt("DEBUG", "Buffer(%lld) - Active(%d) Full(%d) Empty(%d) Jitter(%d us) Count(%d)", buffer->GetTimebase(), buffer->IsActive(), buffer->IsFull(), buffer->IsEmpty(), buffer->GetBufferingSizeUsec(), buffer->GetBufferingSizeCount());

		// If there is no input for a certain period of time, it is considered as an inactive buffer.
		if(buffer->IsActive() == false)
		{
			continue;
		}

		//logc("DEBUG", "Timebase : %u Next PTS : %u", buffer->GetTimebase(), buffer->GetNextPtsUsec());
		if(next_jitter_buffer == nullptr || buffer->GetNextPtsUsec() < next_jitter_buffer->GetNextPtsUsec())
		{
			next_jitter_buffer = buffer;
		}
	}

	// There are no active buffers
	if(next_jitter_buffer == nullptr)
	{
		return nullptr;
	}

	return next_jitter_buffer->PopNextMediaPacket();
}

bool JitterBufferDelay::DoesEveryActiveBufferHavePackets()
{
	if(_media_packet_queue_map.empty())
	{
		return false;
	}

	for(const auto &item : _media_packet_queue_map)
	{
		auto buffer = item.second;
		if(buffer->IsActive() == true && buffer->IsEmpty())
		{
			logt("DEBUG", "Empty Buffer(%lld) - Active(%d) Full(%d) Empty(%d) Jitter(%d us) Count(%d)", buffer->GetTimebase(), buffer->IsActive(), buffer->IsFull(), buffer->IsEmpty(), buffer->GetBufferingSizeUsec(), buffer->GetBufferingSizeCount());

			return false;
		}
	}

	return true;
}

std::shared_ptr<JitterBuffer> JitterBufferDelay::GetFullBuffer()
{
	if(_media_packet_queue_map.empty())
	{
		return nullptr;
	}

	for(const auto &item : _media_packet_queue_map)
	{
		auto buffer = item.second;
		if(buffer->IsFull())
		{
			return buffer;
		}
	}

	return nullptr;
}