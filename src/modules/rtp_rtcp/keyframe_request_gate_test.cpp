//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: KeyframeRequestGate
//
//==============================================================================
#include <gtest/gtest.h>

#include "keyframe_request_gate.h"

namespace
{
using namespace std::chrono_literals;
using TimePoint = KeyframeRequestGate::TimePoint;
constexpr auto kInterval = 3000ms;
constexpr auto kBudget = 600ms;
constexpr uint32_t kTrack = 2;
const TimePoint kT0 = TimePoint{} + 10s;
}  // namespace

TEST(KeyframeRequestGate, RequestsWhenNoKeyframeWasEverReceived)
{
	KeyframeRequestGate gate;
	EXPECT_TRUE(gate.OnFrameDiscarded(kTrack, kT0, kInterval, kBudget, false));
}

TEST(KeyframeRequestGate, NoRequestWhileKeyframeIsRecent)
{
	KeyframeRequestGate gate;
	gate.OnKeyframeReceived(kTrack, kT0);
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0 + 1000ms, kInterval, kBudget, false));
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0 + 2999ms, kInterval, kBudget, false));
}

TEST(KeyframeRequestGate, RequestsOnceKeyframeIsOlderThanInterval)
{
	KeyframeRequestGate gate;
	gate.OnKeyframeReceived(kTrack, kT0);
	EXPECT_TRUE(gate.OnFrameDiscarded(kTrack, kT0 + 3000ms, kInterval, kBudget, false));
}

TEST(KeyframeRequestGate, WaitsForPeriodicRequestInFlight)
{
	KeyframeRequestGate gate;
	gate.OnPeriodicRequestSent(kT0);
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0 + 300ms, kInterval, kBudget, false));
	EXPECT_TRUE(gate.OnFrameDiscarded(kTrack, kT0 + 600ms, kInterval, kBudget, false));
}

TEST(KeyframeRequestGate, WaitsForOwnRequestInFlight)
{
	KeyframeRequestGate gate;
	ASSERT_TRUE(gate.OnFrameDiscarded(kTrack, kT0, kInterval, kBudget, false));
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0 + 300ms, kInterval, kBudget, false));  // burst of give-ups
	EXPECT_TRUE(gate.OnFrameDiscarded(kTrack, kT0 + 600ms, kInterval, kBudget, false));   // still no keyframe, ask again
}

TEST(KeyframeRequestGate, KeyframeAfterRequestClosesTheGate)
{
	KeyframeRequestGate gate;
	ASSERT_TRUE(gate.OnFrameDiscarded(kTrack, kT0, kInterval, kBudget, false));
	gate.OnKeyframeReceived(kTrack, kT0 + 200ms);
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0 + 1000ms, kInterval, kBudget, false));
}

TEST(KeyframeRequestGate, TracksAreIndependent)
{
	KeyframeRequestGate gate;
	gate.OnKeyframeReceived(kTrack, kT0);
	ASSERT_TRUE(gate.OnFrameDiscarded(3, kT0 + 100ms, kInterval, kBudget, false));        // track 3 never got one
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0 + 100ms, kInterval, kBudget, false));  // track 2 is fine
	EXPECT_FALSE(gate.OnFrameDiscarded(3, kT0 + 200ms, kInterval, kBudget, false));       // track 3 request in flight
}

TEST(KeyframeRequestGate, ZeroIntervalMeansEveryGiveUpQualifies)
{
	KeyframeRequestGate gate;
	gate.OnKeyframeReceived(kTrack, kT0);
	EXPECT_TRUE(gate.OnFrameDiscarded(kTrack, kT0 + 1ms, 0ms, kBudget, false));
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0 + 2ms, 0ms, kBudget, false));
	EXPECT_TRUE(gate.OnFrameDiscarded(kTrack, kT0 + 601ms, 0ms, kBudget, false));
}

TEST(KeyframeRequestGate, NoRequestWhileKeyframeIsArriving)
{
	KeyframeRequestGate gate;
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0, kInterval, kBudget, true));          // even with none received yet
	gate.OnKeyframeReceived(kTrack, kT0);
	EXPECT_FALSE(gate.OnFrameDiscarded(kTrack, kT0 + 5000ms, kInterval, kBudget, true));
	EXPECT_TRUE(gate.OnFrameDiscarded(kTrack, kT0 + 5000ms, kInterval, kBudget, false));
}
