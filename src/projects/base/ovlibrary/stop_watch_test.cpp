//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/base/ovlibrary/test_stop_watch.cpp
//  Covers: ov::StopWatch (Start/Stop/Elapsed/IsElapsed/Pause/Resume)
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/stop_watch.h>

#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Basic start / stop
// ---------------------------------------------------------------------------

TEST(OvStopWatch, NotStartedByDefault)
{
	ov::StopWatch sw;
	EXPECT_FALSE(sw.IsStart());
}

TEST(OvStopWatch, IsStartAfterStart)
{
	ov::StopWatch sw;
	sw.Start();
	EXPECT_TRUE(sw.IsStart());
}

TEST(OvStopWatch, ElapsedAfterSleep)
{
	ov::StopWatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	auto elapsed = sw.Elapsed();
	// Should be at least 40ms (be loose to avoid flakiness)
	EXPECT_GE(elapsed, 40);
}

TEST(OvStopWatch, IsElapsed)
{
	ov::StopWatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	EXPECT_TRUE(sw.IsElapsed(50));
	EXPECT_FALSE(sw.IsElapsed(1000));
}

TEST(OvStopWatch, RestartResetsElapsed)
{
	ov::StopWatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	sw.Restart();
	auto elapsed = sw.Elapsed();
	EXPECT_LT(elapsed, 30);
}

// ---------------------------------------------------------------------------
// Pause / Resume
// ---------------------------------------------------------------------------

TEST(OvStopWatch, PauseStopsElapsed)
{
	// ov::StopWatch::Elapsed() subtracts completed pause durations only when
	// Resume() is called. After resume, the 60ms sleep-while-paused should NOT
	// appear in the elapsed time.
	ov::StopWatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	auto before_pause = sw.Elapsed();
	sw.Pause();
	EXPECT_TRUE(sw.IsPaused());
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	sw.Resume();
	EXPECT_FALSE(sw.IsPaused());
	auto after_resume = sw.Elapsed();
	// After resume the paused 60ms is subtracted; elapsed should be close to
	// before_pause (the active 50ms), not ~110ms.
	EXPECT_NEAR(static_cast<double>(after_resume),
	            static_cast<double>(before_pause), 20.0)
	    << "before_pause=" << before_pause << " after_resume=" << after_resume;
}

TEST(OvStopWatch, ResumeAfterPauseContinues)
{
	ov::StopWatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	sw.Pause();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));  // should not count
	sw.Resume();
	EXPECT_FALSE(sw.IsPaused());
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	auto elapsed = sw.Elapsed();
	// Should be around 60ms (30 before pause + 30 after), not 80+
	EXPECT_LT(elapsed, 80);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

TEST(OvStopWatch, UpdateReturnsTrue)
{
	ov::StopWatch sw;
	sw.Start();
	EXPECT_TRUE(sw.Update());
}

TEST(OvStopWatch, UpdateBeforeStartReturnsFalse)
{
	ov::StopWatch sw;
	EXPECT_FALSE(sw.Update());
}

// ---------------------------------------------------------------------------
// ElapsedUs / nanosecond
// ---------------------------------------------------------------------------

TEST(OvStopWatch, ElapsedUsIsMillisTimesThousand)
{
	ov::StopWatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	auto ms = sw.Elapsed();
	auto us = sw.ElapsedUs();
	// us should be roughly 1000x ms
	EXPECT_GE(us, ms * 900);
	EXPECT_LE(us, ms * 1100);
}
