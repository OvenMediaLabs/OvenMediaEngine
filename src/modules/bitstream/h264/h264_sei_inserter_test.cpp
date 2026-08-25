//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: H264TimecodeGenerator - drop frame detection, frame number to
//  timecode conversion, PTS anchoring against a quantized timebase - and the
//  SPS patch state H264SeiInserter keeps per track
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/bitstream/h264/h264_sei_inserter.h>
#include <modules/bitstream/nalu/nal_header.h>
#include <modules/bitstream/nalu/nal_unit_insertor.h>

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
		// Long enough to cross a minute boundary whatever time of day the anchor lands on, so the
		// drop frame skip is exercised on every run rather than only when the clock lines up
		const auto pts_list = QuantizedPts(fps, static_cast<size_t>(65.0 * fps));

		// SMPTE 12M skips the lowest frame numbers at the start of every minute except every
		// tenth: 2 per minute at 30 nominal, 4 at 60
		const int32_t nominal_rate = static_cast<int32_t>(::lround(
			H264TimecodeGenerator::IsDropFrameRate(fps) ? (fps * 1.001) : fps));
		const int64_t max_step = 1 + (nominal_rate / 15);

		int64_t previous  = -1;
		int64_t skips	  = 0;
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
					EXPECT_LE(current - previous, max_step) << "fps " << fps << ", picture " << i;

					if (current - previous > 1)
					{
						skips++;
					}
				}
			}

			previous = current;
		}

		if (H264TimecodeGenerator::IsDropFrameRate(fps) == true)
		{
			EXPECT_GT(skips, 0) << "fps " << fps << ": the window has to cross a minute boundary";
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

// The one branch that reads the tz database. Compared against localtime_r() here rather than a
// fixed offset, so the assertion holds whatever zone the machine runs in.
TEST(H264TimecodeGenerator, LocalReadsTheServerClock)
{
	H264SeiTimecodeZone local;
	ASSERT_TRUE(H264SeiTimecodeZone::Parse("Local", local));
	ASSERT_TRUE(local.local);

	H264SeiClockTimestamp timestamp;
	ASSERT_TRUE(H264TimecodeGenerator(local).Generate(0, k90kHz, 30.0, timestamp));

	struct timespec now = {};
	::clock_gettime(CLOCK_REALTIME, &now);

	struct tm broken_down = {};
	ASSERT_NE(::localtime_r(&now.tv_sec, &broken_down), nullptr);

	const int32_t expected = (((broken_down.tm_hour * 60) + broken_down.tm_min) * 60) + broken_down.tm_sec;
	const int32_t stamped  = (((timestamp.hours * 60) + timestamp.minutes) * 60) + timestamp.seconds;

	// The two clock reads are not quite the same instant, hence the second of slack
	EXPECT_NEAR(((stamped - expected) + 86400) % 86400, 0, 1);
}

// ---------------------------------------------------------------------------
// H264SeiInserter
// ---------------------------------------------------------------------------

namespace
{
	// The 1920x1080 60 fps NVENC parameter sets the rewriter test uses. This SPS already carries
	// pic_struct_present_flag = 1.
	const std::vector<uint8_t> kSps = {
		0x67, 0x42, 0xC0, 0x2A, 0x95, 0xA0, 0x1E, 0x00, 0x89, 0xF9, 0x70, 0x11,
		0x00, 0x00, 0x03, 0x03, 0xE8, 0x00, 0x01, 0xD4, 0xC0, 0xE0, 0x00, 0x00,
		0x13, 0x12, 0xD0, 0x00, 0x04, 0xC4, 0xB5, 0xBB, 0xCB, 0x82, 0x80};
	const std::vector<uint8_t> kPps		 = {0x68, 0xCE, 0x3C, 0x80};
	const std::vector<uint8_t> kIdrSlice = {0x65, 0x88, 0x84, 0x00, 0x21, 0xFF};
	// NAL type 7, but there is nothing behind the header for ParseSPS to read
	const std::vector<uint8_t> kTruncatedSps = {0x67, 0x64};

	std::shared_ptr<ov::Data> AsData(const std::vector<uint8_t> &bytes)
	{
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	// What MakeSpsContext() builds inside the inserter
	H264SeiSpsContext SpsContextOf(const std::vector<uint8_t> &sps_nal)
	{
		H264SPS sps;
		H264SeiSpsContext context;

		if (H264Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps) == false)
		{
			return context;
		}

		context.cpb_dpb_delays_present	= sps.IsCpbDpbDelaysPresent();
		context.cpb_removal_delay_length = sps.GetCpbRemovalDelayLength();
		context.dpb_output_delay_length	= sps.GetDpbOutputDelayLength();
		context.time_offset_length		= sps.GetTimeOffsetLength();
		context.pic_struct_present		= sps.IsPicStructPresent();
		context.frame_mbs_only			= sps.IsFrameMbsOnly();
		context.log2_max_frame_num		= sps.GetLog2MaxFrameNum();
		context.separate_colour_plane	= sps.IsSeparateColourPlane();

		return context;
	}

	// How an encoder that leaves pic_struct_present_flag off emits the same parameter set
	std::shared_ptr<ov::Data> SpsWithoutPicStruct()
	{
		H264SPS sps;
		if (H264Parser::ParseSPS(kSps.data(), kSps.size(), sps) == false)
		{
			return nullptr;
		}

		const size_t bit_pos = sps.GetPicStructPresentFlagBitPos();
		if (bit_pos == 0)
		{
			return nullptr;
		}

		auto cleared = AsData(kSps);
		cleared->GetWritableDataAs<uint8_t>()[bit_pos / 8] &= static_cast<uint8_t>(~(0x80 >> (bit_pos % 8)));

		return cleared;
	}

	// A pic_timing the encoder wrote itself, as it would appear in the bitstream
	std::shared_ptr<ov::Data> PicTimingSeiNal(const H264SeiSpsContext &context, uint8_t pic_struct)
	{
		H264SeiPicTiming pic_timing;
		pic_timing.pic_struct = pic_struct;

		H264SEI sei;
		sei.SetPayloadType(H264SEI::PayloadType::PICTURE_TIMING);
		sei.SetPayloadData(H264SEI::SerializePicTiming(pic_timing, context));

		auto nal = std::make_shared<ov::Data>();
		nal->Append(NalHeader::CreateH264(static_cast<uint8_t>(H264NalUnitType::Sei)));
		nal->Append(sei.Serialize());

		return NalUnitInsertor::EmulationPreventionBytes(nal);
	}

	std::shared_ptr<ov::Data> PicTimingSeiNal(uint8_t pic_struct)
	{
		return PicTimingSeiNal(SpsContextOf(kSps), pic_struct);
	}

	// What the encoder emits while its own SPS still has pic_struct_present_flag off: the delays
	// and nothing else
	std::shared_ptr<ov::Data> DelayOnlyPicTimingSeiNal()
	{
		auto context			   = SpsContextOf(kSps);
		context.pic_struct_present = false;

		return PicTimingSeiNal(context, 0);
	}

	std::shared_ptr<ov::Data> AccessUnit(const std::vector<std::shared_ptr<ov::Data>> &nals)
	{
		const uint8_t start_code[4] = {0x00, 0x00, 0x00, 0x01};

		auto data = std::make_shared<ov::Data>();
		for (const auto &nal : nals)
		{
			data->Append(start_code, sizeof(start_code));
			data->Append(nal);
		}

		return data;
	}

	std::shared_ptr<MediaPacket> MakePacket(const std::shared_ptr<ov::Data> &access_unit, int64_t pts)
	{
		return std::make_shared<MediaPacket>(cmn::MediaType::Video, 0, access_unit, pts, pts, 0,
											 MediaPacketFlag::Key,
											 cmn::BitstreamFormat::H264_ANNEXB,
											 cmn::PacketType::NALU);
	}

	std::shared_ptr<MediaTrack> MakeTrack()
	{
		auto track = std::make_shared<MediaTrack>();

		track->SetId(0);
		track->SetMediaType(cmn::MediaType::Video);
		track->SetCodecId(cmn::MediaCodecId::H264);
		track->SetTimeBase(1, 90000);
		track->SetFrameRateByConfig(60.0);

		return track;
	}

	std::shared_ptr<info::Stream> MakeStream()
	{
		auto stream = std::make_shared<info::Stream>(StreamSourceType::Rtmp);
		stream->SetName("test");

		return stream;
	}

	// pic_struct of the first pic_timing in the access unit
	std::optional<uint32_t> PicStructOf(const std::shared_ptr<const ov::Data> &access_unit)
	{
		NalUnitFragmentHeader fragment_header;
		if (NalUnitFragmentHeader::Parse(access_unit, fragment_header) == false)
		{
			return std::nullopt;
		}

		const auto *fragment = fragment_header.GetFragmentHeader();
		const auto *buffer	 = access_unit->GetDataAs<uint8_t>();
		const auto context	 = SpsContextOf(kSps);

		for (size_t i = 0; i < fragment->GetCount(); i++)
		{
			auto nal = fragment->GetFragment(i);
			if (nal.has_value() == false)
			{
				continue;
			}

			const size_t offset = std::get<0>(nal.value());
			const size_t length = std::get<1>(nal.value());
			if (static_cast<H264NalUnitType>(buffer[offset] & kH264NalUnitTypeMask) != H264NalUnitType::Sei)
			{
				continue;
			}

			auto rbsp = NalUnitInsertor::RemoveEmulationPreventionBytes(
				std::make_shared<ov::Data>(buffer + offset + 1, length - 1));

			std::vector<H264SEI::Message> messages;
			if (H264SEI::SplitMessages(rbsp, messages) == false)
			{
				continue;
			}

			for (const auto &message : messages)
			{
				H264SeiPicTiming pic_timing;
				if ((message.Is(H264SEI::PayloadType::PICTURE_TIMING) == true) &&
					(H264SEI::ParsePicTiming(message.payload, context, pic_timing) == true))
				{
					return pic_timing.pic_struct;
				}
			}
		}

		return std::nullopt;
	}
}  // namespace

// PatchSps() turns pic_struct_present_flag on, so the Replace path has to read the encoder's own
// message with the pre-patch value. That memory must not outlive the SPS it came from: an encoder
// reconfigured mid-stream to emit an already-flagged SPS writes a real pic_struct, and overwriting
// it loses field pairing and frame doubling.
TEST(H264SeiInserter, ForgetsThePatchWhenAnSpsArrivesAlreadyFlagged)
{
	H264SeiInserter inserter;

	auto stream = MakeStream();
	auto track	= MakeTrack();

	auto sps_without_flag = SpsWithoutPicStruct();
	ASSERT_NE(sps_without_flag, nullptr);

	auto patched = MakePacket(AccessUnit({sps_without_flag, AsData(kPps), AsData(kIdrSlice)}), 0);
	ASSERT_TRUE(inserter.InsertPicTiming(stream, track, patched));

	auto flagged = MakePacket(
		AccessUnit({AsData(kSps), AsData(kPps), PicTimingSeiNal(3), AsData(kIdrSlice)}), 1500);
	ASSERT_TRUE(inserter.InsertPicTiming(stream, track, flagged));

	// 3 is "top field, bottom field"; DetectPictureStructure() would have said 0
	EXPECT_EQ(PicStructOf(flagged->GetData()), 3U);
}

// The patch state has to survive an SPS this parser cannot read. DecideSeiPlacement() does not
// refresh _sps_context from one either, so clearing it would read the encoder's delay-only
// pic_timing as if it carried pic_struct - which lands on a pic_struct of 8 (frame tripling) when
// the delay widths happen to leave 7 padding bits, and fails the parse otherwise.
TEST(H264SeiInserter, KeepsThePatchStateWhenAnSpsCannotBeParsed)
{
	H264SeiInserter inserter;

	auto stream = MakeStream();
	auto track	= MakeTrack();

	auto sps_without_flag = SpsWithoutPicStruct();
	ASSERT_NE(sps_without_flag, nullptr);

	auto patched = MakePacket(AccessUnit({sps_without_flag, AsData(kPps), AsData(kIdrSlice)}), 0);
	ASSERT_TRUE(inserter.InsertPicTiming(stream, track, patched));

	auto broken = MakePacket(AccessUnit({AsData(kTruncatedSps), AsData(kPps),
										 DelayOnlyPicTimingSeiNal(), AsData(kIdrSlice)}),
							 1500);
	ASSERT_TRUE(inserter.InsertPicTiming(stream, track, broken))
		<< "the encoder's message has to be read the way it was written";

	EXPECT_EQ(PicStructOf(broken->GetData()), 0U) << "a progressive frame, not what the padding says";
}
