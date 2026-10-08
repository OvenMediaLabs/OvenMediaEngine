#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

// Decides whether a frame the receiver gave up on warrants a keyframe
// request (PLI) to the sender. A request goes out only when the track has
// no recent keyframe (none at all, or older than the periodic keyframe
// interval), no keyframe is already arriving in the receive buffer, and no
// request is in flight (a periodic request or a PLI for the track sent
// within the round trip budget). A periodic interval of zero means no
// periodic keyframes, so every give-up qualifies. Thread safe.
class KeyframeRequestGate
{
public:
	using TimePoint = std::chrono::steady_clock::time_point;

	void OnKeyframeReceived(uint32_t track_id, TimePoint now);
	// A periodic request (FIR) was sent; it covers every track of the stream
	void OnPeriodicRequestSent(TimePoint now);
	// Returns true if a PLI should be sent for the track now, and records it
	bool OnFrameDiscarded(uint32_t track_id, TimePoint now,
						  std::chrono::milliseconds periodic_interval,
						  std::chrono::milliseconds round_trip_budget,
						  bool keyframe_arriving);

private:
	std::mutex _lock;
	std::unordered_map<uint32_t, TimePoint> _last_keyframe_at;
	std::unordered_map<uint32_t, TimePoint> _last_request_at;
	std::optional<TimePoint> _last_periodic_request_at;
};
