//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include "filter_fps.h"
#include "filter_skipframes_controller.h"

// The controller reads no clock and touches nothing outside itself, so a test can hand it
// synthetic time and rates and assert the decision directly.
//
// Two things about the measurement API matter when reading these tests:
//   * Every Add*/Commit call also counts toward the busy window, so a test that cares about
//     utilization either feeds before the window it measures or keeps the values small.
//   * The very first Evaluate() only seeds the timestamps and clears the window. Every test
//     therefore starts with one throwaway call.

namespace
{
	using Decision = SkipFramesController::Decision;

	constexpr int64_t kWindowMs	  = 1001;  // one tick past the 1s evaluation interval
	constexpr int32_t kEmaFrames  = 80;	   // enough commits for the average to reach the target
	constexpr double  kCadenceFps = 30.0;

	// Budgets chosen to land clear of an integer boundary, so rounding cannot flip the level.
	//   138ms -> 6.52fps sustainable -> skip 4
	//   316ms -> 2.85fps sustainable -> skip 10
	constexpr int64_t kBudgetForLevel4Us  = 138000;
	constexpr int64_t kBudgetForLevel10Us = 316000;
	constexpr int64_t kCheapBudgetUs	  = 200;

	SkipFramesController::Observation MakeObservation(double actual_input_fps)
	{
		SkipFramesController::Observation observation;

		observation.expected_input_fps	= kCadenceFps;
		observation.actual_input_fps	= actual_input_fps;
		observation.cadence_fps			= kCadenceFps;
		observation.expected_output_fps	= kCadenceFps;
		observation.actual_output_fps	= kCadenceFps;

		return observation;
	}

	// The threshold is 95% of the expected input rate.
	SkipFramesController::Observation KeepingUp()
	{
		return MakeObservation(kCadenceFps);
	}

	SkipFramesController::Observation FallingBehind()
	{
		return MakeObservation(kCadenceFps * 0.5);
	}

	// Drives the per-frame average to processing_us + handoff_us.
	void FeedFrames(SkipFramesController &controller, int64_t processing_us, int64_t handoff_us)
	{
		for (int32_t i = 0; i < kEmaFrames; i++)
		{
			controller.AddHandoffTime(handoff_us);
			controller.CommitFrame(processing_us);
		}
	}

	int64_t BusyUsFor(double utilization, int64_t window_ms = kWindowMs)
	{
		return static_cast<int64_t>(utilization * static_cast<double>(window_ms) * 1000.0);
	}

	// One saturated window that is also losing input.
	std::optional<SkipFramesController::Result> RunOverloadedWindow(SkipFramesController &controller, int64_t &now)
	{
		controller.AddBusyTime(BusyUsFor(0.99));
		now += kWindowMs;

		return controller.Evaluate(now, FallingBehind());
	}

	// Repeats overloaded windows until the level reaches the target.
	void ClimbTo(SkipFramesController &controller, int32_t target, int64_t &now)
	{
		for (int32_t guard = 0; guard < 64 && controller.GetSkipFrames() < target; guard++)
		{
			RunOverloadedWindow(controller, now);
		}

		ASSERT_EQ(controller.GetSkipFrames(), target);
	}
}  // namespace

TEST(SkipFramesControllerTest, DisabledConfigNeverDecides)
{
	SkipFramesController controller;
	controller.Configure(-1);

	EXPECT_FALSE(controller.IsEnabled());
	EXPECT_FALSE(controller.IsAutomatic());

	EXPECT_FALSE(controller.Evaluate(1000, KeepingUp()).has_value());
	EXPECT_FALSE(controller.Evaluate(1000 + kWindowMs, KeepingUp()).has_value());
}

TEST(SkipFramesControllerTest, FixedConfigNeverDecides)
{
	SkipFramesController controller;
	controller.Configure(3);

	EXPECT_TRUE(controller.IsEnabled());
	EXPECT_FALSE(controller.IsAutomatic());
	EXPECT_EQ(controller.GetSkipFrames(), 3);
	EXPECT_EQ(controller.GetConfiguredSkipFrames(), 3);

	EXPECT_FALSE(controller.Evaluate(1000, FallingBehind()).has_value());
	EXPECT_FALSE(controller.Evaluate(1000 + kWindowMs, FallingBehind()).has_value());
	EXPECT_EQ(controller.GetSkipFrames(), 3);
}

TEST(SkipFramesControllerTest, WaitsForTheEvaluationInterval)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, FallingBehind()).has_value());

	controller.AddBusyTime(BusyUsFor(0.99));
	EXPECT_FALSE(controller.Evaluate(now + 999, FallingBehind()).has_value());
	EXPECT_TRUE(controller.Evaluate(now + kWindowMs, FallingBehind()).has_value());
}

TEST(SkipFramesControllerTest, RaisesOnlyAfterASecondWindowAgrees)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, FallingBehind()).has_value());

	// The budget asks for 4, but one window is a hiccup.
	auto first = RunOverloadedWindow(controller, now);
	ASSERT_TRUE(first.has_value());
	EXPECT_EQ(first->decision, Decision::Unchanged);
	EXPECT_EQ(controller.GetSkipFrames(), 0);

	// The second window agrees, and the level rises by one step - not straight to 4.
	auto second = RunOverloadedWindow(controller, now);
	ASSERT_TRUE(second.has_value());
	EXPECT_EQ(second->decision, Decision::Bottleneck);
	EXPECT_EQ(second->skip_frames, 1);
	EXPECT_EQ(controller.GetSkipFrames(), 1);
}

TEST(SkipFramesControllerTest, ClimbsOneStepPerWindowAndStopsAtTheBudget)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, FallingBehind()).has_value());

	RunOverloadedWindow(controller, now);  // confirmation window
	for (int32_t expected = 1; expected <= 4; expected++)
	{
		auto result = RunOverloadedWindow(controller, now);
		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->decision, Decision::Bottleneck);
		EXPECT_EQ(controller.GetSkipFrames(), expected);
	}

	// The budget wanted 4, so it stops there however long the overload lasts.
	auto settled = RunOverloadedWindow(controller, now);
	ASSERT_TRUE(settled.has_value());
	EXPECT_EQ(settled->decision, Decision::Unchanged);
	EXPECT_EQ(controller.GetSkipFrames(), 4);
}

TEST(SkipFramesControllerTest, DoesNotRaiseBeforeTheInputRateIsMeasured)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel4Us - 1000);

	// A zero input rate means the FPS filter has not measured a second yet. Raising on it
	// would act on a sample that does not exist.
	auto no_sample_yet = MakeObservation(0.0);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, no_sample_yet).has_value());

	for (int32_t i = 0; i < 3; i++)
	{
		controller.AddBusyTime(BusyUsFor(0.99));
		now += kWindowMs;

		auto result = controller.Evaluate(now, no_sample_yet);
		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->decision, Decision::Unchanged);
	}

	EXPECT_EQ(controller.GetSkipFrames(), 0);
}

TEST(SkipFramesControllerTest, DoesNotRaiseWhenTheThreadHasIdleTime)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, FallingBehind()).has_value());

	// The budget looks expensive, but the thread is only half busy - the handoff was waiting
	// on a shallow queue, not running out of capacity.
	for (int32_t i = 0; i < 3; i++)
	{
		controller.AddBusyTime(BusyUsFor(0.50));
		now += kWindowMs;

		auto result = controller.Evaluate(now, FallingBehind());
		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->decision, Decision::Unchanged);
	}

	EXPECT_EQ(controller.GetSkipFrames(), 0);
}

TEST(SkipFramesControllerTest, DoesNotStepDownWhileTheBottleneckStands)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, FallingBehind()).has_value());
	ClimbTo(controller, 2, now);

	// The budget decays between stalls, so on its own it now asks for no skipping at all.
	FeedFrames(controller, kCheapBudgetUs, 0);

	// The thread is still saturated and still losing input, and the recovery hold has long
	// passed - the level must not come down on a window that was just counted as overloaded.
	controller.AddBusyTime(BusyUsFor(0.99, 6000));
	now += 6000;

	auto result = controller.Evaluate(now, FallingBehind());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->decision, Decision::Unchanged);
	EXPECT_EQ(controller.GetSkipFrames(), 2);
}

TEST(SkipFramesControllerTest, HoldsRecoveryUntilTheIntervalPasses)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, FallingBehind()).has_value());
	ClimbTo(controller, 1, now);

	FeedFrames(controller, kCheapBudgetUs, 0);

	// Capacity is back, but the level has only just changed.
	controller.AddBusyTime(BusyUsFor(0.10));
	now += kWindowMs;

	auto held = controller.Evaluate(now, KeepingUp());
	ASSERT_TRUE(held.has_value());
	EXPECT_EQ(held->decision, Decision::HoldRecovery);
	EXPECT_EQ(controller.GetSkipFrames(), 1);

	// Once the hold passes it comes down.
	controller.AddBusyTime(BusyUsFor(0.10, 6000));
	now += 6000;

	auto recovered = controller.Evaluate(now, KeepingUp());
	ASSERT_TRUE(recovered.has_value());
	EXPECT_EQ(recovered->decision, Decision::Recovery);
	EXPECT_EQ(recovered->skip_frames, 0);
	EXPECT_EQ(controller.GetSkipFrames(), 0);
}

TEST(SkipFramesControllerTest, StepsDownWhileBusyIfTheInputIsKeptUp)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, FallingBehind()).has_value());
	ClimbTo(controller, 1, now);

	// Still 99% busy, but the filter is taking in everything that arrives. Utilization alone
	// cannot tell real work from waiting on a two-frame queue, so a level reached during a
	// spike has to be allowed back down.
	controller.AddBusyTime(BusyUsFor(0.99, 6000));
	now += 6000;

	auto result = controller.Evaluate(now, KeepingUp());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->decision, Decision::Recovery);
	EXPECT_EQ(controller.GetSkipFrames(), 0);
}

TEST(SkipFramesControllerTest, RecoveryComesDownTwentyPercentAtATime)
{
	SkipFramesController controller;
	controller.Configure(0);
	FeedFrames(controller, 1000, kBudgetForLevel10Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(controller.Evaluate(now, FallingBehind()).has_value());
	ClimbTo(controller, 10, now);

	FeedFrames(controller, kCheapBudgetUs, 0);

	// 10 - max(1, 10/5) = 8, not straight to 0.
	controller.AddBusyTime(BusyUsFor(0.10, 6000));
	now += 6000;

	auto first = controller.Evaluate(now, KeepingUp());
	ASSERT_TRUE(first.has_value());
	EXPECT_EQ(first->decision, Decision::Recovery);
	EXPECT_EQ(controller.GetSkipFrames(), 8);

	// 8 - max(1, 8/5) = 7
	controller.AddBusyTime(BusyUsFor(0.10, 6000));
	now += 6000;

	auto second = controller.Evaluate(now, KeepingUp());
	ASSERT_TRUE(second.has_value());
	EXPECT_EQ(second->decision, Decision::Recovery);
	EXPECT_EQ(controller.GetSkipFrames(), 7);
}

TEST(SkipFramesControllerTest, InheritKeepsTheLevelAndTheRecoveryHold)
{
	SkipFramesController previous;
	previous.Configure(0);
	FeedFrames(previous, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(previous.Evaluate(now, FallingBehind()).has_value());
	ClimbTo(previous, 1, now);

	SkipFramesController replacement;
	replacement.Configure(0);
	replacement.InheritFrom(previous);

	EXPECT_EQ(replacement.GetSkipFrames(), 1);

	// The hold carries too. Evaluate() re-seeds the timestamps whenever either is still
	// zero, so inheriting only one of the pair would silently restart the interval and the
	// level could never come down on a stream that reconfigures often.
	replacement.AddBusyTime(BusyUsFor(0.10, 6000));
	now += 6000;

	auto result = replacement.Evaluate(now, KeepingUp());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->decision, Decision::Recovery);
	EXPECT_EQ(replacement.GetSkipFrames(), 0);
}

TEST(SkipFramesControllerTest, InheritDoesNotCarryTheLevelToAFixedConfig)
{
	SkipFramesController previous;
	previous.Configure(0);
	FeedFrames(previous, 1000, kBudgetForLevel4Us - 1000);

	int64_t now = 1000;
	ASSERT_FALSE(previous.Evaluate(now, FallingBehind()).has_value());
	ClimbTo(previous, 1, now);

	// The level counts frames within a cadence, so it only transfers between two automatic
	// controllers. A configured level was already applied by Configure().
	SkipFramesController replacement;
	replacement.Configure(3);
	replacement.InheritFrom(previous);

	EXPECT_EQ(replacement.GetSkipFrames(), 3);
}
