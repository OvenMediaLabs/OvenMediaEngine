//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include "fmp4_structure.h"

namespace
{
	constexpr uint64_t kTargetDurationMs = 6000;
	constexpr double kTimebaseSeconds = 1.0 / 90000.0;

	std::shared_ptr<ov::Data> MakePart(uint8_t fill, size_t length)
	{
		std::vector<uint8_t> bytes(length, fill);
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	struct SegmentWithParts
	{
		std::shared_ptr<bmff::FMP4Segment> segment = std::make_shared<bmff::FMP4Segment>(0, kTargetDurationMs, kTimebaseSeconds);
		std::vector<std::shared_ptr<ov::Data>> parts{MakePart('a', 1000), MakePart('b', 1500), MakePart('c', 700)};

		SegmentWithParts()
		{
			int64_t timestamp = 0;
			for (const auto &part : parts)
			{
				EXPECT_TRUE(segment->AppendPartialData(part, timestamp, 1000, timestamp == 0));
				timestamp += 90000;
			}
		}
	};

	TEST(FMP4SegmentBuffer, InProgressSegmentHoldsNoBuffer)
	{
		SegmentWithParts fixture;

		EXPECT_FALSE(fixture.segment->IsCompleted());
		EXPECT_EQ(fixture.segment->GetData(), nullptr);
		EXPECT_EQ(fixture.segment->GetDataLength(), 0u);
		EXPECT_EQ(fixture.segment->GetPartialCount(), fixture.parts.size());
	}

	TEST(FMP4SegmentBuffer, CompletedSegmentIsItsPartsInOrderAtExactSize)
	{
		SegmentWithParts fixture;
		auto before = fixture.segment->GetPartialSegment(1);

		fixture.segment->SetCompleted();

		auto data = fixture.segment->GetData();
		ASSERT_NE(data, nullptr);
		ov::String expected;
		for (const auto &part : fixture.parts)
		{
			expected.Append(part->GetDataAs<char>(), part->GetLength());
		}
		EXPECT_EQ(data->ToString(), expected);
		EXPECT_EQ(fixture.segment->GetDataLength(), expected.GetLength());
		EXPECT_EQ(data->GetCapacity(), data->GetLength());

		size_t offset = 0;
		for (size_t index = 0; index < fixture.parts.size(); index++)
		{
			auto partial = fixture.segment->GetPartialSegment(index);
			ASSERT_NE(partial, nullptr);
			EXPECT_EQ(partial->GetData()->ToString(), fixture.parts[index]->ToString());
			EXPECT_EQ(partial->GetData()->GetDataAs<uint8_t>(), data->GetDataAs<uint8_t>() + offset);
			EXPECT_EQ(partial->GetNumber(), static_cast<int64_t>(index));
			EXPECT_EQ(partial->GetStartTimestamp(), static_cast<int64_t>(index) * 90000);
			EXPECT_EQ(partial->IsIndependent(), index == 0);
			offset += partial->GetDataLength();
		}
		EXPECT_EQ(offset, data->GetLength());

		// A part handed out before completion still serves its own bytes
		EXPECT_EQ(before->GetData()->ToString(), fixture.parts[1]->ToString());
	}

	TEST(FMP4SegmentBuffer, SegmentLoadedFromDvrKeepsItsBuffer)
	{
		auto data = MakePart('d', 4096);
		auto segment = std::make_shared<bmff::FMP4Segment>(7, 6000.0, data);

		EXPECT_TRUE(segment->IsCompleted());
		EXPECT_EQ(segment->GetData(), data);
		EXPECT_EQ(segment->GetDataLength(), 4096u);
		EXPECT_EQ(segment->GetPartialCount(), 0u);
	}
}  // namespace
