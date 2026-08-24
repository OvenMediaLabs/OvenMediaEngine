//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: H264SEI sei_rbsp() message split/join and the pic_timing codec
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/bitstream/h264/h264_sei.h>

namespace
{
	std::shared_ptr<ov::Data> MakeData(const std::vector<uint8_t> &bytes)
	{
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	// The SPS context NVENC produces: HRD present, no pic_struct
	H264SeiSpsContext NvencLikeContext()
	{
		H264SeiSpsContext context;

		context.cpb_dpb_delays_present	 = true;
		context.cpb_removal_delay_length = 16;
		context.dpb_output_delay_length	 = 6;
		context.time_offset_length		 = 24;
		context.pic_struct_present		 = false;

		return context;
	}
}  // namespace

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------

TEST(H264Sei, SerializeWritesTypeSizeAndTrailingBits)
{
	H264SEI sei;
	sei.SetPayloadType(H264SEI::PayloadType::USER_DATA_UNREGISTERED);
	sei.SetPayloadData(MakeData({0xAA, 0xBB, 0xCC}));

	auto rbsp = sei.Serialize();
	ASSERT_NE(rbsp, nullptr);

	// payloadType(1) + payloadSize(1) + payload(3) + rbsp_trailing_bits(1)
	ASSERT_EQ(rbsp->GetLength(), 6U);

	const auto *bytes = rbsp->GetDataAs<uint8_t>();
	EXPECT_EQ(bytes[0], 5);		// USER_DATA_UNREGISTERED
	EXPECT_EQ(bytes[1], 3);		// payloadSize
	EXPECT_EQ(bytes[2], 0xAA);
	EXPECT_EQ(bytes[5], 0x80);	// rbsp_trailing_bits()
}

TEST(H264Sei, SerializeUsesTheFfContinuationForLargePayloads)
{
	// ITU-T H.264 7.3.2.3.1: payloadSize is a sequence of 0xFF bytes plus a remainder
	H264SEI sei;
	sei.SetPayloadType(H264SEI::PayloadType::USER_DATA_UNREGISTERED);
	sei.SetPayloadData(MakeData(std::vector<uint8_t>(300, 0x11)));

	auto rbsp = sei.Serialize();
	ASSERT_NE(rbsp, nullptr);

	const auto *bytes = rbsp->GetDataAs<uint8_t>();
	EXPECT_EQ(bytes[0], 5);
	EXPECT_EQ(bytes[1], 0xFF);
	EXPECT_EQ(bytes[2], 300 - 255);
	// payloadType(1) + payloadSize(2) + payload(300) + trailing(1)
	EXPECT_EQ(rbsp->GetLength(), 304U);
}

TEST(H264Sei, SplitAndJoinAreInverse)
{
	std::vector<H264SEI::Message> messages;

	H264SEI::Message buffering_period;
	buffering_period.payload_type = static_cast<uint32_t>(H264SEI::PayloadType::BUFFERING_PERIOD);
	buffering_period.payload	  = MakeData({0x01, 0x02});
	messages.push_back(buffering_period);

	H264SEI::Message pic_timing;
	pic_timing.payload_type = static_cast<uint32_t>(H264SEI::PayloadType::PICTURE_TIMING);
	pic_timing.payload		= MakeData({0x03, 0x04, 0x05});
	messages.push_back(pic_timing);

	auto rbsp = H264SEI::JoinMessages(messages);
	ASSERT_NE(rbsp, nullptr);

	std::vector<H264SEI::Message> parsed;
	ASSERT_TRUE(H264SEI::SplitMessages(rbsp, parsed));
	ASSERT_EQ(parsed.size(), 2U);

	EXPECT_TRUE(parsed[0].Is(H264SEI::PayloadType::BUFFERING_PERIOD));
	EXPECT_TRUE(parsed[1].Is(H264SEI::PayloadType::PICTURE_TIMING));
	EXPECT_TRUE(parsed[0].payload->IsEqual(messages[0].payload));
	EXPECT_TRUE(parsed[1].payload->IsEqual(messages[1].payload));
}

TEST(H264Sei, SplitRejectsTruncatedInput)
{
	std::vector<H264SEI::Message> parsed;

	// payloadType 5, payloadSize 4, but only two payload bytes follow
	EXPECT_FALSE(H264SEI::SplitMessages(MakeData({0x05, 0x04, 0x01, 0x02}), parsed));
	EXPECT_FALSE(H264SEI::SplitMessages(nullptr, parsed));
}

// ---------------------------------------------------------------------------
// pic_timing codec
// ---------------------------------------------------------------------------

TEST(H264SeiPicTiming, RoundTripsAClockTimestamp)
{
	auto context				= NvencLikeContext();
	context.pic_struct_present	= true;

	H264SeiPicTiming source;
	source.cpb_removal_delay = 1234;
	source.dpb_output_delay	 = 7;
	source.pic_struct		 = 0;

	H264SeiClockTimestamp timestamp;
	timestamp.ct_type			  = 0;
	timestamp.counting_type		  = 1;
	timestamp.full_timestamp_flag = true;
	timestamp.discontinuity_flag  = false;
	timestamp.cnt_dropped_flag	  = false;
	timestamp.n_frames			  = 42;
	timestamp.hours				  = 19;
	timestamp.minutes			  = 51;
	timestamp.seconds			  = 57;
	timestamp.time_offset_length  = context.time_offset_length;
	source.clock_timestamps.push_back(timestamp);

	auto payload = H264SEI::SerializePicTiming(source, context);
	ASSERT_NE(payload, nullptr);

	H264SeiPicTiming parsed;
	ASSERT_TRUE(H264SEI::ParsePicTiming(payload, context, parsed));

	EXPECT_EQ(parsed.cpb_removal_delay, source.cpb_removal_delay);
	EXPECT_EQ(parsed.dpb_output_delay, source.dpb_output_delay);
	EXPECT_EQ(parsed.pic_struct, source.pic_struct);
	ASSERT_EQ(parsed.clock_timestamps.size(), 1U);

	const auto &out = parsed.clock_timestamps[0];
	EXPECT_EQ(out.hours, 19);
	EXPECT_EQ(out.minutes, 51);
	EXPECT_EQ(out.seconds, 57);
	EXPECT_EQ(out.n_frames, 42);
	EXPECT_EQ(out.counting_type, 1);
	EXPECT_EQ(out.GetTimecodeString(), "19:51:57:42");
}

// The message an encoder writes while pic_struct_present_flag is 0 carries only the delays. Reading
// it as if pic_struct were there walks off the end of the payload, which is what happens when the
// SPS has since been patched and the pre-patch context is not kept.
TEST(H264SeiPicTiming, DelayOnlyPayloadMustBeReadWithTheEncodersContext)
{
	auto encoder_context = NvencLikeContext();

	H264SeiPicTiming source;
	source.cpb_removal_delay = 900;
	source.dpb_output_delay	 = 3;

	auto payload = H264SEI::SerializePicTiming(source, encoder_context);
	ASSERT_NE(payload, nullptr);
	// cpb(16) + dpb(6) = 22 bits -> 3 bytes once the trailing bits are added
	EXPECT_EQ(payload->GetLength(), 3U);

	H264SeiPicTiming parsed;
	ASSERT_TRUE(H264SEI::ParsePicTiming(payload, encoder_context, parsed));
	EXPECT_EQ(parsed.cpb_removal_delay, 900U);
	EXPECT_EQ(parsed.dpb_output_delay, 3U);
	EXPECT_TRUE(parsed.clock_timestamps.empty());

	auto patched_context			  = encoder_context;
	patched_context.pic_struct_present = true;

	H264SeiPicTiming misparsed;
	EXPECT_FALSE(H264SEI::ParsePicTiming(payload, patched_context, misparsed))
		<< "The same bytes must not be readable as if they carried pic_struct";
}

TEST(H264SeiPicTiming, RejectsAReservedPicStruct)
{
	auto context			  = NvencLikeContext();
	context.pic_struct_present = true;

	H264SeiPicTiming source;
	source.pic_struct = 9;	// reserved, NumClockTS is undefined

	EXPECT_EQ(H264SEI::SerializePicTiming(source, context), nullptr);
}

TEST(H264SeiPicTiming, RejectsAnInvalidSpsContext)
{
	H264SeiSpsContext context;
	context.cpb_removal_delay_length = 33;	// coded in u(5), so 32 is the ceiling

	EXPECT_FALSE(context.IsValid());
	EXPECT_EQ(H264SEI::SerializePicTiming(H264SeiPicTiming(), context), nullptr);

	H264SeiPicTiming parsed;
	EXPECT_FALSE(H264SEI::ParsePicTiming(MakeData({0x00, 0x00}), context, parsed));
}

TEST(H264SeiPicTiming, NumClockTsFollowsTableD1)
{
	EXPECT_EQ(H264SeiPicTiming::NumClockTSFromPicStruct(0), 1);	 // progressive frame
	EXPECT_EQ(H264SeiPicTiming::NumClockTSFromPicStruct(1), 1);	 // top field
	EXPECT_EQ(H264SeiPicTiming::NumClockTSFromPicStruct(2), 1);	 // bottom field
	EXPECT_EQ(H264SeiPicTiming::NumClockTSFromPicStruct(3), 2);
	EXPECT_EQ(H264SeiPicTiming::NumClockTSFromPicStruct(7), 2);	 // frame doubling
	EXPECT_EQ(H264SeiPicTiming::NumClockTSFromPicStruct(8), 3);	 // frame tripling
	EXPECT_EQ(H264SeiPicTiming::NumClockTSFromPicStruct(9), 0);	 // reserved
}

// ---------------------------------------------------------------------------
// H264SeiTimecodeZone
// ---------------------------------------------------------------------------

namespace
{
	H264SeiTimecodeZone ParsedZone(const char *text)
	{
		// Deliberately not the default, so a Parse() that fails silently is visible
		H264SeiTimecodeZone zone;
		zone.local			= true;
		zone.offset_seconds = 1;

		EXPECT_TRUE(H264SeiTimecodeZone::Parse(text, zone)) << text;

		return zone;
	}
}  // namespace

// The same stream has to carry the same timecode on every node it runs on, so the server's own
// zone is opt in rather than the fallback
TEST(H264SeiTimecodeZone, UtcIsTheDefault)
{
	H264SeiTimecodeZone zone;
	EXPECT_FALSE(zone.local);
	EXPECT_EQ(zone.offset_seconds, 0);
	EXPECT_EQ(zone.ToString(), "UTC");

	// An absent <Timezone> reads as an empty string
	for (const char *text : {"", "  ", "UTC", "utc", "Z", "z", "+00:00", "-00:00", "+0000", "+00"})
	{
		auto parsed = ParsedZone(text);
		EXPECT_FALSE(parsed.local) << text;
		EXPECT_EQ(parsed.offset_seconds, 0) << text;
		EXPECT_EQ(parsed.ToString(), "UTC") << text;
	}
}

TEST(H264SeiTimecodeZone, LocalHasToBeAskedFor)
{
	for (const char *text : {"Local", "local", "LOCAL", "  Local  "})
	{
		auto parsed = ParsedZone(text);
		EXPECT_TRUE(parsed.local) << text;
		EXPECT_EQ(parsed.ToString(), "Local") << text;
	}
}

TEST(H264SeiTimecodeZone, ReadsAFixedOffset)
{
	for (const char *text : {"+09:00", "+0900", "+09"})
	{
		auto zone = ParsedZone(text);
		EXPECT_FALSE(zone.local) << text;
		EXPECT_EQ(zone.offset_seconds, 9 * 3600) << text;
		EXPECT_EQ(zone.ToString(), "+09:00") << text;
	}

	// Every offset the sample EventInfo.xml puts in front of a user
	EXPECT_EQ(ParsedZone("+05:30").offset_seconds, (5 * 3600) + (30 * 60));
	EXPECT_EQ(ParsedZone("+05:45").offset_seconds, (5 * 3600) + (45 * 60));
	EXPECT_EQ(ParsedZone("-03:00").offset_seconds, -3 * 3600);
	EXPECT_EQ(ParsedZone("-05:00").offset_seconds, -5 * 3600);

	EXPECT_EQ(ParsedZone("-09:30").ToString(), "-09:30");
	EXPECT_EQ(ParsedZone("+14:00").offset_seconds, 14 * 3600) << "the widest offset in use";
}

TEST(H264SeiTimecodeZone, RejectsWhatItCannotRead)
{
	H264SeiTimecodeZone zone;

	for (const char *text : {"Asia/Seoul", "KST", "GMT", "9", "09:00", "+9:00", "+09:0",
							 "+ab:cd", "+09:60", "+15:00", "-15:00", "+09:00:00"})
	{
		EXPECT_FALSE(H264SeiTimecodeZone::Parse(text, zone)) << text;
	}

	// A rejected text leaves the value alone
	zone.local			= false;
	zone.offset_seconds = 3600;
	EXPECT_FALSE(H264SeiTimecodeZone::Parse("Asia/Seoul", zone));
	EXPECT_FALSE(zone.local);
	EXPECT_EQ(zone.offset_seconds, 3600);
}

TEST(H264SeiTimecodeZone, ToStringRoundTrips)
{
	for (const char *text : {"Local", "UTC", "+09:00", "-05:00", "+05:45", "-09:30", "+14:00"})
	{
		auto zone = ParsedZone(text);

		H264SeiTimecodeZone again;
		ASSERT_TRUE(H264SeiTimecodeZone::Parse(zone.ToString(), again)) << text;
		EXPECT_EQ(again.local, zone.local) << text;
		EXPECT_EQ(again.offset_seconds, zone.offset_seconds) << text;
	}
}
