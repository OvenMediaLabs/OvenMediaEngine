#include <gtest/gtest.h>

#define private public
#include "../../modules/rtmp/chunk/rtmp_chunk_parser.h"
#include "../../modules/rtmp_v2/chunk/rtmp_chunk_parser.h"
#undef private

#include <vector>

namespace
{
	constexpr const char *TEST_APP_STREAM_NAME = "dummy/stream";

	std::shared_ptr<const ov::Data> ToData(const std::vector<uint8_t> &bytes)
	{
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	RtmpChunkParser::ParseResult ParseChunk(RtmpChunkParser &parser, const std::vector<uint8_t> &bytes, size_t &bytes_used)
	{
		return parser.Parse(ToData(bytes), &bytes_used);
	}

	modules::rtmp::ChunkParser::ParseResult ParseChunkV2(modules::rtmp::ChunkParser &parser, const std::vector<uint8_t> &bytes, size_t &bytes_used)
	{
		return parser.Parse(ToData(bytes), &bytes_used);
	}

	std::vector<uint8_t> MakeType0ExtendedChunk(uint32_t extended_timestamp, uint32_t stream_id = 1U, uint8_t chunk_stream_id = 3U)
	{
		return {
			chunk_stream_id,
			0xFF,
			0xFF,
			0xFF,
			0x00,
			0x00,
			0x00,
			static_cast<uint8_t>(ov::ToUnderlyingType(RtmpMessageTypeID::Amf0Command)),
			static_cast<uint8_t>(stream_id & 0xFF),
			static_cast<uint8_t>((stream_id >> 8) & 0xFF),
			static_cast<uint8_t>((stream_id >> 16) & 0xFF),
			static_cast<uint8_t>((stream_id >> 24) & 0xFF),
			static_cast<uint8_t>((extended_timestamp >> 24) & 0xFF),
			static_cast<uint8_t>((extended_timestamp >> 16) & 0xFF),
			static_cast<uint8_t>((extended_timestamp >> 8) & 0xFF),
			static_cast<uint8_t>(extended_timestamp & 0xFF),
		};
	}

	std::vector<uint8_t> MakeType1ExtendedChunk(uint32_t extended_timestamp_delta, uint32_t message_length = 0U, uint8_t chunk_stream_id = 3U)
	{
		return {
			static_cast<uint8_t>(0x40 | chunk_stream_id),
			0xFF,
			0xFF,
			0xFF,
			static_cast<uint8_t>((message_length >> 16) & 0xFF),
			static_cast<uint8_t>((message_length >> 8) & 0xFF),
			static_cast<uint8_t>(message_length & 0xFF),
			static_cast<uint8_t>(ov::ToUnderlyingType(RtmpMessageTypeID::Amf0Command)),
			static_cast<uint8_t>((extended_timestamp_delta >> 24) & 0xFF),
			static_cast<uint8_t>((extended_timestamp_delta >> 16) & 0xFF),
			static_cast<uint8_t>((extended_timestamp_delta >> 8) & 0xFF),
			static_cast<uint8_t>(extended_timestamp_delta & 0xFF),
		};
	}

	std::vector<uint8_t> MakeType1ExtendedChunkWithPayload(uint32_t extended_timestamp_delta, const std::vector<uint8_t> &payload, uint8_t chunk_stream_id = 3U)
	{
		auto chunk = MakeType1ExtendedChunk(extended_timestamp_delta, static_cast<uint32_t>(payload.size()), chunk_stream_id);
		chunk.insert(chunk.end(), payload.begin(), payload.end());
		return chunk;
	}

	std::vector<uint8_t> MakeType1ExtendedChunkWithMessageLengthAndPayload(uint32_t extended_timestamp_delta, uint32_t message_length, const std::vector<uint8_t> &payload, uint8_t chunk_stream_id = 3U)
	{
		auto chunk = MakeType1ExtendedChunk(extended_timestamp_delta, message_length, chunk_stream_id);
		chunk.insert(chunk.end(), payload.begin(), payload.end());
		return chunk;
	}

	std::vector<uint8_t> MakeType3ExtendedChunk(uint32_t extended_timestamp_delta, uint8_t chunk_stream_id = 3U)
	{
		return {
			static_cast<uint8_t>(0xC0 | chunk_stream_id),
			static_cast<uint8_t>((extended_timestamp_delta >> 24) & 0xFF),
			static_cast<uint8_t>((extended_timestamp_delta >> 16) & 0xFF),
			static_cast<uint8_t>((extended_timestamp_delta >> 8) & 0xFF),
			static_cast<uint8_t>(extended_timestamp_delta & 0xFF),
		};
	}

	TEST(RtmpChunkParser, ResolveExtendedPositiveDeltaAsUnsigned)
	{
		RtmpChunkParser parser(128);

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		const auto resolved_timestamp = parser.ResolveTimestampDelta(
			1U,
			3000,
			0x00010000U,
			true);

		ASSERT_TRUE(resolved_timestamp.has_value());
		EXPECT_EQ(resolved_timestamp.value(), 3000 + 0x00010000LL);
	}

	TEST(RtmpChunkParser, ResolveSmallNegativeExtendedDeltaAsSignedCompatibilityPath)
	{
		RtmpChunkParser parser(128);

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		const auto resolved_timestamp = parser.ResolveTimestampDelta(
			1U,
			3000,
			0xFFFFFFF3U,
			true);

		ASSERT_TRUE(resolved_timestamp.has_value());
		EXPECT_EQ(resolved_timestamp.value(), 2987);
	}

	TEST(RtmpChunkParser, RejectNegativeResolvedTimestampFromSignedCompatibilityPath)
	{
		RtmpChunkParser parser(128);

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		const auto resolved_timestamp = parser.ResolveTimestampDelta(
			1U,
			3000,
			0xFFFFEC78U,
			true);

		EXPECT_FALSE(resolved_timestamp.has_value());
	}

	TEST(RtmpChunkParser, ResolveWrappedType0TimestampForward)
	{
		RtmpChunkParser parser(128);

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			4294967000LL,
			1000);

		EXPECT_EQ(resolved_timestamp, 4294968296LL);
	}

	TEST(RtmpChunkParser, KeepBackwardType0TimestampBackward)
	{
		RtmpChunkParser parser(128);

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			3000,
			2000);

		EXPECT_EQ(resolved_timestamp, 2000);
	}

	TEST(RtmpChunkParser, ResolveType0TimestampAsBackwardAtHalfRangeBoundary)
	{
		RtmpChunkParser parser(128);

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			0,
			0x80000000ULL);

		EXPECT_EQ(resolved_timestamp, -0x80000000LL);
	}

	TEST(RtmpChunkParser, KeepType0TimestampForwardWithinHalfRangeInCurrentEpoch)
	{
		RtmpChunkParser parser(128);

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			1000,
			0x800003E6ULL);

		EXPECT_EQ(resolved_timestamp, 0x800003E6LL);
	}

	TEST(RtmpChunkParser, RollWrappedType0TimestampForwardWhenBackwardDistanceExceedsHalfRange)
	{
		RtmpChunkParser parser(128);

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			0x80000001LL,
			0);

		EXPECT_EQ(resolved_timestamp, 0x100000000LL);
	}

	TEST(RtmpChunkParser, ParseType1ExtendedSignedDeltaCompatibilityPath)
	{
		RtmpChunkParser parser(128);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(3000), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_EQ(bytes_used, MakeType0ExtendedChunk(3000).size());
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType1ExtendedChunk(0xFFFFFFF3U), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_EQ(bytes_used, MakeType1ExtendedChunk(0xFFFFFFF3U).size());

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 2987);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFF3U);
	}

	TEST(RtmpChunkParser, ParseType1ExtendedSignedDeltaRejectsNegativeAbsoluteTimestamp)
	{
		RtmpChunkParser parser(128);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(3000), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType1ExtendedChunk(0xFFFFEC78U), bytes_used), RtmpChunkParser::ParseResult::Error);
		EXPECT_EQ(bytes_used, 0UL);
		EXPECT_EQ(parser.GetMessage(), nullptr);
	}

	TEST(RtmpChunkParser, ParseType1ExtendedTimestampReturnsNeedMoreDataWhenExtendedFieldIsIncomplete)
	{
		RtmpChunkParser parser(128);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(3000), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		const std::vector<uint8_t> partial_type1 = {
			0x43,
			0xFF,
			0xFF,
			0xFF,
			0x00,
			0x00,
			0x00,
			static_cast<uint8_t>(ov::ToUnderlyingType(RtmpMessageTypeID::Amf0Command)),
			0xFF,
			0xFF,
		};

		EXPECT_EQ(ParseChunk(parser, partial_type1, bytes_used), RtmpChunkParser::ParseResult::NeedMoreData);
		EXPECT_EQ(bytes_used, 0UL);
		EXPECT_EQ(parser.GetMessage(), nullptr);
	}

	TEST(RtmpChunkParser, ParseType1ExtendedTimestampRecoversAfterNeedMoreData)
	{
		RtmpChunkParser parser(128);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(3000), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		const std::vector<uint8_t> partial_type1 = {
			0x43,
			0xFF,
			0xFF,
			0xFF,
			0x00,
			0x00,
			0x00,
			static_cast<uint8_t>(ov::ToUnderlyingType(RtmpMessageTypeID::Amf0Command)),
			0xFF,
			0xFF,
		};

		EXPECT_EQ(ParseChunk(parser, partial_type1, bytes_used), RtmpChunkParser::ParseResult::NeedMoreData);
		EXPECT_EQ(bytes_used, 0UL);

		EXPECT_EQ(ParseChunk(parser, MakeType1ExtendedChunk(0xFFFFFFF3U), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_EQ(bytes_used, MakeType1ExtendedChunk(0xFFFFFFF3U).size());

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 2987);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFF3U);
	}

	TEST(RtmpChunkParser, ParseType3ExtendedSignedDeltaCompatibilityPath)
	{
		RtmpChunkParser parser(128);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(3000), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType1ExtendedChunk(0xFFFFFFF3U), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType3ExtendedChunk(0xFFFFFFF3U), bytes_used), RtmpChunkParser::ParseResult::Parsed);

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 2974);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFF3U);
	}

	TEST(RtmpChunkParser, ParseType0RejectsNegativeAbsoluteTimestampAfterBackwardResolution)
	{
		RtmpChunkParser parser(128);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(1000), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(0xFFFFFF00U), bytes_used), RtmpChunkParser::ParseResult::Error);
		EXPECT_EQ(bytes_used, 0UL);
		EXPECT_EQ(parser.GetMessage(), nullptr);
	}

	TEST(RtmpChunkParser, ParseType3ContinuationDoesNotApplyDeltaTwice)
	{
		RtmpChunkParser parser(2);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(100), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}).size());
		EXPECT_EQ(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, std::vector<uint8_t>{0xC3, 0xFF, 0xFF, 0xFF, 0xA6, 0x33, 0x44}, bytes_used), RtmpChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, 7UL);

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 10);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFA6U);
		ASSERT_EQ(message->payload->GetLength(), 4U);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[0], 0x11);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[1], 0x22);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[2], 0x33);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[3], 0x44);
	}

	TEST(RtmpChunkParser, ParseType3ContinuationIgnoresMismatchedExtendedField)
	{
		RtmpChunkParser parser(2);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(100), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		EXPECT_EQ(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, std::vector<uint8_t>{0xC3, 0xFF, 0xFF, 0xFF, 0xA5, 0x33, 0x44}, bytes_used), RtmpChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, 7UL);

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 10);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFA6U);
		ASSERT_EQ(message->payload->GetLength(), 4U);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[2], 0x33);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[3], 0x44);
	}

	TEST(RtmpChunkParser, ParseType3ContinuationRecoversAfterNeedMoreData)
	{
		RtmpChunkParser parser(2);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(100), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		EXPECT_EQ(parser.GetMessage(), nullptr);

		const std::vector<uint8_t> partial_type3 = {
			0xC3,
			0xFF,
			0xFF,
		};

		EXPECT_EQ(ParseChunk(parser, partial_type3, bytes_used), RtmpChunkParser::ParseResult::NeedMoreData);
		EXPECT_EQ(bytes_used, 0UL);

		EXPECT_EQ(ParseChunk(parser, std::vector<uint8_t>{0xC3, 0xFF, 0xFF, 0xFF, 0xA6, 0x33, 0x44}, bytes_used), RtmpChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, 7UL);

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 10);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFA6U);
		ASSERT_EQ(message->payload->GetLength(), 4U);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[2], 0x33);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[3], 0x44);
	}

	TEST(RtmpChunkParser, ParseType3ContinuationFromPendingMessageDoesNotApplyDeltaTwice)
	{
		RtmpChunkParser parser(2);
		size_t bytes_used = 0;

		parser.UpdateNamePath(info::NamePath(TEST_APP_STREAM_NAME));

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(100), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}, 3U), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}, 3U).size());
		EXPECT_EQ(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunk(parser, MakeType0ExtendedChunk(200, 1U, 4U), bytes_used), RtmpChunkParser::ParseResult::Parsed);
		const auto interleaved_message = parser.GetMessage();
		ASSERT_NE(interleaved_message, nullptr);
		EXPECT_EQ(interleaved_message->header->basic_header.chunk_stream_id, 4U);
		EXPECT_EQ(interleaved_message->header->completed.timestamp, 200);

		EXPECT_EQ(ParseChunk(parser, std::vector<uint8_t>{0xC3, 0xFF, 0xFF, 0xFF, 0xA6, 0x33, 0x44}, bytes_used), RtmpChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, 7UL);

		const auto resumed_message = parser.GetMessage();
		ASSERT_NE(resumed_message, nullptr);
		EXPECT_EQ(resumed_message->header->basic_header.chunk_stream_id, 3U);
		EXPECT_EQ(resumed_message->header->completed.timestamp, 10);
		EXPECT_EQ(resumed_message->header->completed.timestamp_delta, 0xFFFFFFA6U);
		ASSERT_EQ(resumed_message->payload->GetLength(), 4U);
		EXPECT_EQ(resumed_message->payload->GetDataAs<uint8_t>()[0], 0x11);
		EXPECT_EQ(resumed_message->payload->GetDataAs<uint8_t>()[1], 0x22);
		EXPECT_EQ(resumed_message->payload->GetDataAs<uint8_t>()[2], 0x33);
		EXPECT_EQ(resumed_message->payload->GetDataAs<uint8_t>()[3], 0x44);
	}

	TEST(RtmpChunkParserV2, ResolveExtendedPositiveDeltaAsUnsigned)
	{
		modules::rtmp::ChunkParser parser(128);

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		const auto resolved_timestamp = parser.ResolveTimestampDelta(
			1U,
			3000,
			0x00010000U,
			true);

		ASSERT_TRUE(resolved_timestamp.has_value());
		EXPECT_EQ(resolved_timestamp.value(), 3000 + 0x00010000LL);
	}

	TEST(RtmpChunkParserV2, ResolveSmallNegativeExtendedDeltaAsSignedCompatibilityPath)
	{
		modules::rtmp::ChunkParser parser(128);

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		const auto resolved_timestamp = parser.ResolveTimestampDelta(
			1U,
			3000,
			0xFFFFFFF3U,
			true);

		ASSERT_TRUE(resolved_timestamp.has_value());
		EXPECT_EQ(resolved_timestamp.value(), 2987);
	}

	TEST(RtmpChunkParserV2, RejectNegativeResolvedTimestampFromSignedCompatibilityPath)
	{
		modules::rtmp::ChunkParser parser(128);

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		const auto resolved_timestamp = parser.ResolveTimestampDelta(
			1U,
			3000,
			0xFFFFEC78U,
			true);

		EXPECT_FALSE(resolved_timestamp.has_value());
	}

	TEST(RtmpChunkParserV2, ResolveWrappedType0TimestampForward)
	{
		modules::rtmp::ChunkParser parser(128);

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			4294967000LL,
			1000);

		EXPECT_EQ(resolved_timestamp, 4294968296LL);
	}

	TEST(RtmpChunkParserV2, KeepBackwardType0TimestampBackward)
	{
		modules::rtmp::ChunkParser parser(128);

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			3000,
			2000);

		EXPECT_EQ(resolved_timestamp, 2000);
	}

	TEST(RtmpChunkParserV2, ResolveType0TimestampAsBackwardAtHalfRangeBoundary)
	{
		modules::rtmp::ChunkParser parser(128);

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			0,
			0x80000000ULL);

		EXPECT_EQ(resolved_timestamp, -0x80000000LL);
	}

	TEST(RtmpChunkParserV2, KeepType0TimestampForwardWithinHalfRangeInCurrentEpoch)
	{
		modules::rtmp::ChunkParser parser(128);

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			1000,
			0x800003E6ULL);

		EXPECT_EQ(resolved_timestamp, 0x800003E6LL);
	}

	TEST(RtmpChunkParserV2, RollWrappedType0TimestampForwardWhenBackwardDistanceExceedsHalfRange)
	{
		modules::rtmp::ChunkParser parser(128);

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		const auto resolved_timestamp = parser.CalculateRolledTimestamp(
			1U,
			0x80000001LL,
			0);

		EXPECT_EQ(resolved_timestamp, 0x100000000LL);
	}

	TEST(RtmpChunkParserV2, ParseType1ExtendedSignedDeltaCompatibilityPath)
	{
		modules::rtmp::ChunkParser parser(128);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(3000), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_EQ(bytes_used, MakeType0ExtendedChunk(3000).size());
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType1ExtendedChunk(0xFFFFFFF3U), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_EQ(bytes_used, MakeType1ExtendedChunk(0xFFFFFFF3U).size());

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 2987);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFF3U);
	}

	TEST(RtmpChunkParserV2, ParseType1ExtendedSignedDeltaRejectsNegativeAbsoluteTimestamp)
	{
		modules::rtmp::ChunkParser parser(128);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(3000), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType1ExtendedChunk(0xFFFFEC78U), bytes_used), modules::rtmp::ChunkParser::ParseResult::Error);
		EXPECT_EQ(bytes_used, 0UL);
		EXPECT_EQ(parser.GetMessage(), nullptr);
	}

	TEST(RtmpChunkParserV2, ParseType1ExtendedTimestampReturnsNeedMoreDataWhenExtendedFieldIsIncomplete)
	{
		modules::rtmp::ChunkParser parser(128);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(3000), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		const std::vector<uint8_t> partial_type1 = {
			0x43,
			0xFF,
			0xFF,
			0xFF,
			0x00,
			0x00,
			0x00,
			static_cast<uint8_t>(ov::ToUnderlyingType(modules::rtmp::MessageTypeID::Amf0Command)),
			0xFF,
			0xFF,
		};

		EXPECT_EQ(ParseChunkV2(parser, partial_type1, bytes_used), modules::rtmp::ChunkParser::ParseResult::NeedMoreData);
		EXPECT_EQ(bytes_used, 0UL);
		EXPECT_EQ(parser.GetMessage(), nullptr);
	}

	TEST(RtmpChunkParserV2, ParseType1ExtendedTimestampRecoversAfterNeedMoreData)
	{
		modules::rtmp::ChunkParser parser(128);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(3000), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		const std::vector<uint8_t> partial_type1 = {
			0x43,
			0xFF,
			0xFF,
			0xFF,
			0x00,
			0x00,
			0x00,
			static_cast<uint8_t>(ov::ToUnderlyingType(modules::rtmp::MessageTypeID::Amf0Command)),
			0xFF,
			0xFF,
		};

		EXPECT_EQ(ParseChunkV2(parser, partial_type1, bytes_used), modules::rtmp::ChunkParser::ParseResult::NeedMoreData);
		EXPECT_EQ(bytes_used, 0UL);

		EXPECT_EQ(ParseChunkV2(parser, MakeType1ExtendedChunk(0xFFFFFFF3U), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_EQ(bytes_used, MakeType1ExtendedChunk(0xFFFFFFF3U).size());

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 2987);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFF3U);
	}

	TEST(RtmpChunkParserV2, ParseType3ExtendedSignedDeltaCompatibilityPath)
	{
		modules::rtmp::ChunkParser parser(128);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(3000), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType1ExtendedChunk(0xFFFFFFF3U), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType3ExtendedChunk(0xFFFFFFF3U), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 2974);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFF3U);
	}

	TEST(RtmpChunkParserV2, ParseType0RejectsNegativeAbsoluteTimestampAfterBackwardResolution)
	{
		modules::rtmp::ChunkParser parser(128);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(1000), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(0xFFFFFF00U), bytes_used), modules::rtmp::ChunkParser::ParseResult::Error);
		EXPECT_EQ(bytes_used, 0UL);
		EXPECT_EQ(parser.GetMessage(), nullptr);
	}

	TEST(RtmpChunkParserV2, ParseType3ContinuationDoesNotApplyDeltaTwice)
	{
		modules::rtmp::ChunkParser parser(2);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(100), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}).size());
		EXPECT_EQ(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, std::vector<uint8_t>{0xC3, 0xFF, 0xFF, 0xFF, 0xA6, 0x33, 0x44}, bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, 7UL);

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 10);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFA6U);
		ASSERT_EQ(message->payload->GetLength(), 4U);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[0], 0x11);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[1], 0x22);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[2], 0x33);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[3], 0x44);
	}

	TEST(RtmpChunkParserV2, ParseType3ContinuationIgnoresMismatchedExtendedField)
	{
		modules::rtmp::ChunkParser parser(2);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(100), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		EXPECT_EQ(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, std::vector<uint8_t>{0xC3, 0xFF, 0xFF, 0xFF, 0xA5, 0x33, 0x44}, bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, 7UL);

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 10);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFA6U);
		ASSERT_EQ(message->payload->GetLength(), 4U);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[2], 0x33);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[3], 0x44);
	}

	TEST(RtmpChunkParserV2, ParseType3ContinuationRecoversAfterNeedMoreData)
	{
		modules::rtmp::ChunkParser parser(2);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(100), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		EXPECT_EQ(parser.GetMessage(), nullptr);

		const std::vector<uint8_t> partial_type3 = {
			0xC3,
			0xFF,
			0xFF,
		};

		EXPECT_EQ(ParseChunkV2(parser, partial_type3, bytes_used), modules::rtmp::ChunkParser::ParseResult::NeedMoreData);
		EXPECT_EQ(bytes_used, 0UL);

		EXPECT_EQ(ParseChunkV2(parser, std::vector<uint8_t>{0xC3, 0xFF, 0xFF, 0xFF, 0xA6, 0x33, 0x44}, bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, 7UL);

		const auto message = parser.GetMessage();
		ASSERT_NE(message, nullptr);
		EXPECT_EQ(message->header->completed.timestamp, 10);
		EXPECT_EQ(message->header->completed.timestamp_delta, 0xFFFFFFA6U);
		ASSERT_EQ(message->payload->GetLength(), 4U);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[2], 0x33);
		EXPECT_EQ(message->payload->GetDataAs<uint8_t>()[3], 0x44);
	}

	TEST(RtmpChunkParserV2, ParseType3ContinuationFromPendingMessageDoesNotApplyDeltaTwice)
	{
		modules::rtmp::ChunkParser parser(2);
		size_t bytes_used = 0;

		parser.SetMessageQueueAlias(TEST_APP_STREAM_NAME);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(100), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		ASSERT_NE(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}, 3U), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, MakeType1ExtendedChunkWithMessageLengthAndPayload(0xFFFFFFA6U, 4U, {0x11, 0x22}, 3U).size());
		EXPECT_EQ(parser.GetMessage(), nullptr);

		EXPECT_EQ(ParseChunkV2(parser, MakeType0ExtendedChunk(200, 1U, 4U), bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		const auto interleaved_message = parser.GetMessage();
		ASSERT_NE(interleaved_message, nullptr);
		EXPECT_EQ(interleaved_message->header->basic_header.chunk_stream_id, 4U);
		EXPECT_EQ(interleaved_message->header->completed.timestamp, 200);

		EXPECT_EQ(ParseChunkV2(parser, std::vector<uint8_t>{0xC3, 0xFF, 0xFF, 0xFF, 0xA6, 0x33, 0x44}, bytes_used), modules::rtmp::ChunkParser::ParseResult::Parsed);
		EXPECT_EQ(bytes_used, 7UL);

		const auto resumed_message = parser.GetMessage();
		ASSERT_NE(resumed_message, nullptr);
		EXPECT_EQ(resumed_message->header->basic_header.chunk_stream_id, 3U);
		EXPECT_EQ(resumed_message->header->completed.timestamp, 10);
		EXPECT_EQ(resumed_message->header->completed.timestamp_delta, 0xFFFFFFA6U);
		ASSERT_EQ(resumed_message->payload->GetLength(), 4U);
		EXPECT_EQ(resumed_message->payload->GetDataAs<uint8_t>()[0], 0x11);
		EXPECT_EQ(resumed_message->payload->GetDataAs<uint8_t>()[1], 0x22);
		EXPECT_EQ(resumed_message->payload->GetDataAs<uint8_t>()[2], 0x33);
		EXPECT_EQ(resumed_message->payload->GetDataAs<uint8_t>()[3], 0x44);
	}
}  // namespace
