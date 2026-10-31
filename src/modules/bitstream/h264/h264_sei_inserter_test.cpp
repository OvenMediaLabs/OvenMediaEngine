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
	constexpr double k90kHz			 = 1.0 / 90000.0;
	constexpr int32_t kSecondsPerDay = 24 * 60 * 60;

	// FilterFps hands its output slots back in the input timebase, so a 60 fps track on a 1/1000
	// input steps 16, 17, 17 ms. Scaled to 1/90000 that is 1440, 1530, 1530 instead of a uniform
	// 1500. Measured from a live stream.
	std::vector<int64_t> QuantizedPts(double fps, size_t count)
	{
		std::vector<int64_t> pts;
		for (size_t k = 0; k < count; k++)
		{
			pts.push_back(static_cast<int64_t>(k * 1000.0 / fps) * 90);
		}

		return pts;
	}

	// Anchors at a fixed time of day instead of the wall clock, so a test decides where in the
	// day, and where within a frame, the anchor lands. Only the tests that exist to check the clock
	// read itself use the real generator.
	class FixedClockTimecodeGenerator : public H264TimecodeGenerator
	{
	public:
		explicit FixedClockTimecodeGenerator(double time_of_day = 0.0)
			: _time_of_day(time_of_day)
		{
		}

	protected:
		double GetTimeOfDaySeconds() const override
		{
			return _time_of_day;
		}

	private:
		double _time_of_day;
	};

	struct Timecode
	{
		int hours	= 0;
		int minutes = 0;
		int seconds = 0;
		int frames	= 0;

		bool operator==(const Timecode &other) const
		{
			return (hours == other.hours) && (minutes == other.minutes) &&
				   (seconds == other.seconds) && (frames == other.frames);
		}
	};

	// gtest prints both sides of a failed comparison with this
	std::ostream &operator<<(std::ostream &os, const Timecode &timecode)
	{
		return os << ov::String::FormatString("%02d:%02d:%02d:%02d", timecode.hours, timecode.minutes, timecode.seconds, timecode.frames).CStr();
	}

	Timecode ToTimecode(int64_t frame_number, int32_t nominal_fps, bool drop_frame)
	{
		uint8_t hours = 0, minutes = 0, seconds = 0;
		uint16_t frames = 0;
		H264TimecodeGenerator::FrameNumberToTimecode(frame_number, nominal_fps, drop_frame, hours, minutes, seconds, frames);

		return {hours, minutes, seconds, frames};
	}

	// The counting rate: 30 for 29.97, as Generate() derives it
	int32_t NominalRate(double fps)
	{
		return static_cast<int32_t>(::lround(H264TimecodeGenerator::IsDropFrameRate(fps) ? (fps * 1.001) : fps));
	}

	int32_t SecondsOfDay(const H264SeiClockTimestamp &timestamp)
	{
		return (((timestamp.hours * 60) + timestamp.minutes) * 60) + timestamp.seconds;
	}

	// n_frames alone wraps every second, so the absolute count is rebuilt from the whole timecode
	int64_t FrameCount(const H264SeiClockTimestamp &timestamp, int32_t nominal_fps)
	{
		return (static_cast<int64_t>(SecondsOfDay(timestamp)) * nominal_fps) + timestamp.n_frames;
	}

	// later - earlier on a clock that wraps at midnight, as -43200..43199, so two readings either
	// side of midnight still come out close
	int32_t SecondsApart(int32_t later, int32_t earlier)
	{
		const int32_t half_day = kSecondsPerDay / 2;
		return ((((later - earlier) % kSecondsPerDay) + kSecondsPerDay + half_day) % kSecondsPerDay) - half_day;
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
	EXPECT_EQ(ToTimecode(0, 30, false), (Timecode{0, 0, 0, 0}));
	EXPECT_EQ(ToTimecode((30 * 61) + 7, 30, false), (Timecode{0, 1, 1, 7}));
	EXPECT_EQ(ToTimecode(30 * 3600, 30, false), (Timecode{1, 0, 0, 0}));
}

TEST(H264TimecodeGenerator, NonDropFrameWrapsAtTwentyFourHours)
{
	EXPECT_EQ(ToTimecode(30 * kSecondsPerDay, 30, false), (Timecode{0, 0, 0, 0}))
		<< "hours_value is u(5) and must stay below 24";
}

TEST(H264TimecodeGenerator, DropFrameSkipsNumbersAtMinuteBoundaries)
{
	// SMPTE 12M at 30 nominal: minute 0 holds 1800 frame numbers, and the next minute opens at 02
	// because 00 and 01 are skipped.
	EXPECT_EQ(ToTimecode(1799, 30, true), (Timecode{0, 0, 59, 29}));
	EXPECT_EQ(ToTimecode(1800, 30, true), (Timecode{0, 1, 0, 2}));

	// The tenth minute keeps 00 and 01, so it holds the full 1800 numbers and the eleventh opens at
	// 02 again. Only past this point does a wrong ten minute block length show.
	EXPECT_EQ(ToTimecode(17982, 30, true), (Timecode{0, 10, 0, 0}));
	EXPECT_EQ(ToTimecode(19781, 30, true), (Timecode{0, 10, 59, 29}));
	EXPECT_EQ(ToTimecode(19782, 30, true), (Timecode{0, 11, 0, 2}));

	// Six blocks make exactly one hour
	EXPECT_EQ(ToTimecode(107891, 30, true), (Timecode{0, 59, 59, 29}));
	EXPECT_EQ(ToTimecode(107892, 30, true), (Timecode{1, 0, 0, 0}));

	// 60 nominal skips four numbers instead of two
	EXPECT_EQ(ToTimecode(3599, 60, true), (Timecode{0, 0, 59, 59}));
	EXPECT_EQ(ToTimecode(3600, 60, true), (Timecode{0, 1, 0, 4}));
	EXPECT_EQ(ToTimecode(35964, 60, true), (Timecode{0, 10, 0, 0}));
	EXPECT_EQ(ToTimecode(39563, 60, true), (Timecode{0, 10, 59, 59}));
	EXPECT_EQ(ToTimecode(39564, 60, true), (Timecode{0, 11, 0, 4}));
	EXPECT_EQ(ToTimecode(215784, 60, true), (Timecode{1, 0, 0, 0}));
}

// lround() of a measured frame rate below 0.5 is 0, and every line of the conversion divides by it
TEST(H264TimecodeGenerator, FrameNumberToTimecodeSurvivesAZeroCountingRate)
{
	EXPECT_EQ(ToTimecode(100, 0, false).frames, 0);
	EXPECT_EQ(ToTimecode(100, -5, true).frames, 0);
}

// ---------------------------------------------------------------------------
// Generate
// ---------------------------------------------------------------------------

TEST(H264TimecodeGenerator, GenerateNeedsAFrameRateAndATimebase)
{
	FixedClockTimecodeGenerator generator;
	H264SeiClockTimestamp timestamp;

	EXPECT_FALSE(generator.Generate(0, k90kHz, 0.0, timestamp));
	EXPECT_FALSE(generator.Generate(0, k90kHz, -1.0, timestamp));
	EXPECT_FALSE(generator.Generate(0, 0.0, 60.0, timestamp));
}

TEST(H264TimecodeGenerator, GenerateFlagsTheFirstPictureAsDiscontinuous)
{
	FixedClockTimecodeGenerator generator;
	H264SeiClockTimestamp timestamp;

	ASSERT_TRUE(generator.Generate(0, k90kHz, 60.0, timestamp));
	EXPECT_TRUE(timestamp.discontinuity_flag);

	ASSERT_TRUE(generator.Generate(1500, k90kHz, 60.0, timestamp));
	EXPECT_FALSE(timestamp.discontinuity_flag);

	// Reset() drops the anchor, so the next picture starts a new run
	generator.Reset();
	ASSERT_TRUE(generator.Generate(3000, k90kHz, 60.0, timestamp));
	EXPECT_TRUE(timestamp.discontinuity_flag);
}

TEST(H264TimecodeGenerator, GenerateReAnchorsOnAPtsJump)
{
	FixedClockTimecodeGenerator generator;
	H264SeiClockTimestamp timestamp;

	ASSERT_TRUE(generator.Generate(0, k90kHz, 60.0, timestamp));
	ASSERT_TRUE(generator.Generate(1500, k90kHz, 60.0, timestamp));

	// Ten seconds ahead is a break in the presentation timeline, not elapsed time
	ASSERT_TRUE(generator.Generate(1500 + (90000 * 10), k90kHz, 60.0, timestamp));
	EXPECT_TRUE(timestamp.discontinuity_flag);

	// Backwards too
	ASSERT_TRUE(generator.Generate(0, k90kHz, 60.0, timestamp));
	EXPECT_TRUE(timestamp.discontinuity_flag);
}

TEST(H264TimecodeGenerator, GenerateReportsTheCountingType)
{
	H264SeiClockTimestamp timestamp;

	ASSERT_TRUE(FixedClockTimecodeGenerator().Generate(0, k90kHz, 60.0, timestamp));
	EXPECT_EQ(timestamp.counting_type, 1) << "non-drop frame";
	EXPECT_TRUE(timestamp.full_timestamp_flag);

	ASSERT_TRUE(FixedClockTimecodeGenerator().Generate(0, k90kHz, 60000.0 / 1001.0, timestamp));
	EXPECT_EQ(timestamp.counting_type, 2) << "drop frame";
}

// The regression this guards: the PTS is quantized to the track's timebase, so the elapsed part
// never lands exactly on a frame boundary. Rounding it together with the anchor - a wall clock
// reading whose sub-frame phase is arbitrary - dropped a frame number whenever that phase sat near
// the halfway point.
TEST(H264TimecodeGenerator, FrameNumbersAdvanceByOneOnAQuantizedTimebase)
{
	// The regression only shows when the anchor's sub-frame phase sits just past the halfway point,
	// within a window as narrow as the quantization error (0.016 of a frame at 24 fps), so every
	// rate is run at each hundredth of a frame period
	constexpr int32_t kPhaseSteps = 100;

	for (const double fps : {60.0, 50.0, 30.0, 25.0, 24.0, 60000.0 / 1001.0, 30000.0 / 1001.0})
	{
		const bool drop_frame = H264TimecodeGenerator::IsDropFrameRate(fps);
		const int32_t nominal = NominalRate(fps);
		// SMPTE 12M skips 2 numbers per minute at 30 nominal, 4 at 60
		const int64_t max_step = 1 + (nominal / 15);

		const auto pts_list = QuantizedPts(fps, static_cast<size_t>(4.0 * fps));

		for (int32_t phase = 0; phase < kPhaseSteps; phase++)
		{
			// From 00:00:58 the window crosses 00:01:00, a minute that drops numbers, and stays
			// clear of midnight
			FixedClockTimecodeGenerator generator(58.0 + (static_cast<double>(phase) / kPhaseSteps / fps));

			int64_t previous = -1;
			int64_t skips	 = 0;
			for (size_t i = 0; i < pts_list.size(); i++)
			{
				H264SeiClockTimestamp timestamp;
				ASSERT_TRUE(generator.Generate(pts_list[i], k90kHz, fps, timestamp)) << "fps " << fps;

				const int64_t current = FrameCount(timestamp, nominal);
				if (previous >= 0)
				{
					const int64_t step = current - previous;

					// Drop frame counting skips numbers at minute boundaries, so only the plain
					// rates are required to step by exactly one
					if (drop_frame == false)
					{
						ASSERT_EQ(step, 1) << "fps " << fps << ", phase " << phase << ", picture " << i << ", pts " << pts_list[i];
					}
					else
					{
						ASSERT_GE(step, 1) << "fps " << fps << ", phase " << phase << ", picture " << i;
						ASSERT_LE(step, max_step) << "fps " << fps << ", phase " << phase << ", picture " << i;

						if (step > 1)
						{
							skips++;
						}
					}
				}

				previous = current;
			}

			if (drop_frame == true)
			{
				EXPECT_EQ(skips, 1) << "fps " << fps << ", phase " << phase << ": the window crosses one minute boundary";
			}
		}
	}
}

// ---------------------------------------------------------------------------
// The timezone the anchor is read in. These read the real clock on purpose:
// GetTimeOfDaySeconds() is what they check.
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

	H264SeiClockTimestamp from_utc;
	H264SeiClockTimestamp from_shifted;
	ASSERT_TRUE(H264TimecodeGenerator(utc).Generate(0, k90kHz, 30.0, from_utc));
	ASSERT_TRUE(H264TimecodeGenerator(plus_nine).Generate(0, k90kHz, 30.0, from_shifted));

	// Both anchors are the same instant read in two zones, so they sit exactly nine hours apart.
	// The two clock reads are not quite the same instant, hence the second of slack.
	EXPECT_NEAR(SecondsApart(SecondsOfDay(from_shifted), SecondsOfDay(from_utc)), 9 * 3600, 1);
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

	// The two clock reads are not quite the same instant, hence the second of slack
	EXPECT_NEAR(SecondsApart(SecondsOfDay(timestamp), expected), 0, 1);
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
