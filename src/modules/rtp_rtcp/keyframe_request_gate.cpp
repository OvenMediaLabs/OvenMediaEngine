#include "keyframe_request_gate.h"

void KeyframeRequestGate::OnKeyframeReceived(uint32_t track_id, TimePoint now)
{
	std::lock_guard<std::mutex> lock(_lock);
	_last_keyframe_at[track_id] = now;
}

void KeyframeRequestGate::OnPeriodicRequestSent(TimePoint now)
{
	std::lock_guard<std::mutex> lock(_lock);
	_last_periodic_request_at = now;
}

bool KeyframeRequestGate::OnFrameDiscarded(uint32_t track_id, TimePoint now,
										   std::chrono::milliseconds periodic_interval,
										   std::chrono::milliseconds round_trip_budget,
										   bool keyframe_arriving)
{
	std::lock_guard<std::mutex> lock(_lock);

	// A keyframe already in the receive buffer repairs the picture by itself
	if (keyframe_arriving)
	{
		return false;
	}

	// A keyframe within the periodic interval means the next one is due soon
	auto keyframe_it = _last_keyframe_at.find(track_id);
	if (keyframe_it != _last_keyframe_at.end() && (now - keyframe_it->second) < periodic_interval)
	{
		return false;
	}

	// A request sent within one round trip may still be answered
	if (_last_periodic_request_at.has_value() && (now - *_last_periodic_request_at) < round_trip_budget)
	{
		return false;
	}
	auto request_it = _last_request_at.find(track_id);
	if (request_it != _last_request_at.end() && (now - request_it->second) < round_trip_budget)
	{
		return false;
	}

	_last_request_at[track_id] = now;
	return true;
}
