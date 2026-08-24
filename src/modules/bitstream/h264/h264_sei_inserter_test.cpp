//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: H264TimecodeGenerator - drop frame detection, frame number to
//  timecode conversion, and PTS anchoring against a quantized timebase
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/bitstream/h264/h264_sei_inserter.h>

namespace
{
	constexpr double k90kHz = 1.0 / 90000.0;

	// FilterFps hands its output slots back in the input timebase, so a 60 fps track on a 1/1000
	// input steps 16, 17, 17 ms. Scaled to 1/90000 that is 1440, 1530, 1530 instead of a uniform
	// 1500. Measured from a live stream.
	std::vector<int64_t> QuantizedPts(double fps, size_t count, int64_t base = 0)
	{
		std::vector<int64_t> pts;
		for (size_t k = 0; k < count; k++)
		{
			pts.push_back(base + (static_cast<int64_t>(k * 1000.0 / fps) * 90));
		}

		return pts;
	}
}  // namespace

// ---------------------------------------------------------------------------
// IsDropFrameRate
// ---------------------------------------------------------------------------

TEST(H264TimecodeGenerator, DropFrameOnlyForThe1000Over1001Rates)
{
	EXPECT_TRUE(H264TimecodeGenerator::IsDropFrameRate(30000.0 / 1001.0));
	EXPECT_TRUE(H264TimecodeGenerator::IsDropFrameRate(60000.0 / 1001.0));

	EXPECT_FALSE(H264TimecodeGenerator::IsDropFrameRate(30.0));
	EXPECT_FALSE(H264TimecodeGenerator::IsDropFrameRate(60.0));
	EXPECT_FALSE(H264TimecodeGenerator::IsDropFrameRate(25.0));
	// SMPTE defines no drop frame counting for 23.976
	EXPECT_FALSE(H264TimecodeGenerator::IsDropFrameRate(24000.0 / 1001.0));
	EXPECT_FALSE(H264TimecodeGenerator::IsDropFrameRate(0.0));
	EXPECT_FALSE(H264TimecodeGenerator::IsDropFrameRate(-30.0));
}

// ---------------------------------------------------------------------------
// FrameNumberToTimecode
// ---------------------------------------------------------------------------

TEST(H264TimecodeGenerator, NonDropFrameCountsPlainly)
{
	uint8_t hours = 0, minutes = 0, seconds = 0;
	uint16_t frames = 0;

	H264TimecodeGenerator::FrameNumberToTimecode(0, 30, false, hours, minutes, seconds, frames);
	EXPECT_EQ(hours, 0);
	EXPECT_EQ(minutes, 0);
	EXPECT_EQ(seconds, 0);
	EXPECT_EQ(frames, 0);

	// One hour at 30 fps
	H264TimecodeGenerator::FrameNumberToTimecode(30 * 3600, 30, false, hours, minutes, seconds, frames);
	EXPECT_EQ(hours, 1);
	EXPECT_EQ(minutes, 0);
	EXPECT_EQ(seconds, 0);
	EXPECT_EQ(frames, 0);

	H264TimecodeGenerator::FrameNumberToTimecode((30 * 61) + 7, 30, false, hours, minutes, seconds, frames);
	EXPECT_EQ(minutes, 1);
	EXPECT_EQ(seconds, 1);
	EXPECT_EQ(frames, 7);
}

TEST(H264TimecodeGenerator, NonDropFrameWrapsAtTwentyFourHours)
{
	uint8_t hours = 0, minutes = 0, seconds = 0;
	uint16_t frames = 0;

	H264TimecodeGenerator::FrameNumberToTimecode(30 * 3600 * 24, 30, false, hours, minutes, seconds, frames);
	EXPECT_EQ(hours, 0) << "hours_value is u(5) and must stay below 24";
}

TEST(H264TimecodeGenerator, DropFrameSkipsNumbersAtMinuteBoundaries)
{
	uint8_t hours = 0, minutes = 0, seconds = 0;
	uint16_t frames = 0;

	// SMPTE 12M at 30 nominal: minute 0 holds 1800 frame numbers, and the next minute opens at 02
	// because 00 and 01 are skipped.
	H264TimecodeGenerator::FrameNumberToTimecode(1799, 30, true, hours, minutes, seconds, frames);
	EXPECT_EQ(minutes, 0);
	EXPECT_EQ(seconds, 59);
	EXPECT_EQ(frames, 29);

	H264TimecodeGenerator::FrameNumberToTimecode(1800, 30, true, hours, minutes, seconds, frames);
	EXPECT_EQ(minutes, 1);
	EXPECT_EQ(seconds, 0);
	EXPECT_EQ(frames, 2) << "00 and 01 are dropped";

	// The tenth minute keeps 00 and 01
	H264TimecodeGenerator::FrameNumberToTimecode(17982, 30, true, hours, minutes, seconds, frames);
	EXPECT_EQ(minutes, 10);
	EXPECT_EQ(seconds, 0);
	EXPECT_EQ(frames, 0);
}

// lround() of a measured frame rate below 0.5 is 0, and every line of the conversion divides by it
TEST(H264TimecodeGenerator, FrameNumberToTimecodeSurvivesAZeroCountingRate)
{
	uint8_t hours = 0, minutes = 0, seconds = 0;
	uint16_t frames = 0;

	H264TimecodeGenerator::FrameNumberToTimecode(100, 0, false, hours, minutes, seconds, frames);
	EXPECT_EQ(frames, 0);

	H264TimecodeGenerator::FrameNumberToTimecode(100, -5, true, hours, minutes, seconds, frames);
	EXPECT_EQ(frames, 0);
}

// ---------------------------------------------------------------------------
// Generate
// ---------------------------------------------------------------------------

TEST(H264TimecodeGenerator, GenerateNeedsAFrameRateAndATimebase)
{
	H264TimecodeGenerator generator;
	H264SeiClockTimestamp timestamp;

	EXPECT_FALSE(generator.Generate(0, k90kHz, 0.0, timestamp));
	EXPECT_FALSE(generator.Generate(0, k90kHz, -1.0, timestamp));
	EXPECT_FALSE(generator.Generate(0, 0.0, 60.0, timestamp));
}

TEST(H264TimecodeGenerator, GenerateFlagsTheFirstPictureAsDiscontinuous)
{
	H264TimecodeGenerator generator;
	H264SeiClockTimestamp first;
	H264SeiClockTimestamp second;

	ASSERT_TRUE(generator.Generate(0, k90kHz, 60.0, first));
	EXPECT_TRUE(first.discontinuity_flag);

	ASSERT_TRUE(generator.Generate(1500, k90kHz, 60.0, second));
	EXPECT_FALSE(second.discontinuity_flag);

	// Reset() drops the anchor, so the next picture starts a new run
	generator.Reset();
	H264SeiClockTimestamp after_reset;
	ASSERT_TRUE(generator.Generate(3000, k90kHz, 60.0, after_reset));
	EXPECT_TRUE(after_reset.discontinuity_flag);
}

TEST(H264TimecodeGenerator, GenerateReAnchorsOnAPtsJump)
{
	H264TimecodeGenerator generator;
	H264SeiClockTimestamp timestamp;

	ASSERT_TRUE(generator.Generate(0, k90kHz, 60.0, timestamp));
	ASSERT_TRUE(generator.Generate(1500, k90kHz, 60.0, timestamp));
	EXPECT_FALSE(timestamp.discontinuity_flag);

	// Ten seconds ahead is a break in the presentation timeline, not elapsed time
	ASSERT_TRUE(generator.Generate(1500 + (90000 * 10), k90kHz, 60.0, timestamp));
	EXPECT_TRUE(timestamp.discontinuity_flag);

	// Backwards too
	ASSERT_TRUE(generator.Generate(0, k90kHz, 60.0, timestamp));
	EXPECT_TRUE(timestamp.discontinuity_flag);
}

TEST(H264TimecodeGenerator, GenerateReportsTheCountingType)
{
	H264TimecodeGenerator generator;
	H264SeiClockTimestamp timestamp;

	ASSERT_TRUE(generator.Generate(0, k90kHz, 60.0, timestamp));
	EXPECT_EQ(timestamp.counting_type, 1) << "non-drop frame";
	EXPECT_TRUE(timestamp.full_timestamp_flag);

	H264TimecodeGenerator drop_frame_generator;
	ASSERT_TRUE(drop_frame_generator.Generate(0, k90kHz, 60000.0 / 1001.0, timestamp));
	EXPECT_EQ(timestamp.counting_type, 2) << "drop frame";
}

// The regression this guards: the PTS is quantized to the track's timebase, so the elapsed part
// never lands exactly on a frame boundary. Rounding it together with the anchor - a wall clock
// reading whose sub-frame phase is arbitrary - dropped a frame number whenever that phase sat near
// the halfway point.
TEST(H264TimecodeGenerator, FrameNumbersAdvanceByOneOnAQuantizedTimebase)
{
	for (const double fps : {60.0, 50.0, 30.0, 25.0, 24.0, 60000.0 / 1001.0, 30000.0 / 1001.0})
	{
		H264TimecodeGenerator generator;
		const auto pts_list = QuantizedPts(fps, 300);

		int64_t previous = -1;
		for (size_t i = 0; i < pts_list.size(); i++)
		{
			H264SeiClockTimestamp timestamp;
			ASSERT_TRUE(generator.Generate(pts_list[i], k90kHz, fps, timestamp)) << "fps " << fps;

			// n_frames alone wraps every second, so rebuild the absolute count from the timecode
			const int32_t nominal = static_cast<int32_t>(::lround(
				H264TimecodeGenerator::IsDropFrameRate(fps) ? (fps * 1.001) : fps));
			const int64_t current = (((timestamp.hours * 60LL) + timestamp.minutes) * 60LL + timestamp.seconds) * nominal + timestamp.n_frames;

			if (previous >= 0)
			{
				// Drop frame counting skips numbers at minute boundaries, so only the plain rates
				// are required to step by exactly one
				if (H264TimecodeGenerator::IsDropFrameRate(fps) == false)
				{
					EXPECT_EQ(current - previous, 1)
						<< "fps " << fps << ", picture " << i << ", pts " << pts_list[i];
				}
				else
				{
					EXPECT_GE(current - previous, 1) << "fps " << fps << ", picture " << i;
					EXPECT_LE(current - previous, 3) << "fps " << fps << ", picture " << i;
				}
			}

			previous = current;
		}
	}
}

TEST(H264TimecodeGenerator, FrameNumbersDoNotGoBackwards)
{
	H264TimecodeGenerator generator;
	const auto pts_list = QuantizedPts(60.0, 600, 10806030);

	ov::String previous;
	for (const auto pts : pts_list)
	{
		H264SeiClockTimestamp timestamp;
		ASSERT_TRUE(generator.Generate(pts, k90kHz, 60.0, timestamp));

		auto current = timestamp.GetTimecodeString();
		if (previous.IsEmpty() == false)
		{
			// Zero padded HH:MM:SS:FF, so lexical order is chronological order
			EXPECT_GE(::strcmp(current.CStr(), previous.CStr()), 0)
				<< previous.CStr() << " -> " << current.CStr();
		}

		previous = current;
	}
}

// ---------------------------------------------------------------------------
// The timezone the anchor is read in
// ---------------------------------------------------------------------------

TEST(H264TimecodeGenerator, DefaultsToUtc)
{
	EXPECT_FALSE(H264TimecodeGenerator().GetTimezone().local);
	EXPECT_EQ(H264TimecodeGenerator().GetTimezone().offset_seconds, 0);
}

TEST(H264TimecodeGenerator, TheTimezoneMovesTheAnchor)
{
	H264SeiTimecodeZone utc;
	ASSERT_TRUE(H264SeiTimecodeZone::Parse("UTC", utc));

	H264SeiTimecodeZone plus_nine;
	ASSERT_TRUE(H264SeiTimecodeZone::Parse("+09:00", plus_nine));

	H264TimecodeGenerator utc_generator(utc);
	H264TimecodeGenerator shifted_generator(plus_nine);

	H264SeiClockTimestamp from_utc;
	H264SeiClockTimestamp from_shifted;
	ASSERT_TRUE(utc_generator.Generate(0, k90kHz, 30.0, from_utc));
	ASSERT_TRUE(shifted_generator.Generate(0, k90kHz, 30.0, from_shifted));

	auto seconds_of_day = [](const H264SeiClockTimestamp &timestamp) {
		return (((timestamp.hours * 60) + timestamp.minutes) * 60) + timestamp.seconds;
	};

	// Both anchors are the same instant read in two zones, so they sit exactly nine hours apart.
	// The two clock reads are not quite the same instant, hence the second of slack.
	const int32_t difference = ((seconds_of_day(from_shifted) - seconds_of_day(from_utc)) + 86400) % 86400;
	EXPECT_NEAR(difference, 9 * 3600, 1);
}
