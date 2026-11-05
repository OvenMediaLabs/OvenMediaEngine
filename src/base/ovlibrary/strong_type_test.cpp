//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/ovlibrary/strong_type.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <type_traits>

namespace
{
	using FirstId  = ov::StrongType<uint64_t, struct FirstIdTag>;
	using SecondId = ov::StrongType<uint64_t, struct SecondIdTag>;

	// Neither a plain value nor a type with another tag converts implicitly
	static_assert(std::is_convertible_v<uint64_t, FirstId> == false);
	static_assert(std::is_convertible_v<FirstId, uint64_t> == false);
	static_assert(std::is_convertible_v<SecondId, FirstId> == false);
	static_assert(std::is_constructible_v<FirstId, SecondId> == false);

	// It holds nothing but the value
	static_assert(sizeof(FirstId) == sizeof(uint64_t));
	static_assert(std::is_trivially_copyable_v<FirstId>);
}  // namespace

TEST(StrongTypeTest, ComparesByValue)
{
	EXPECT_EQ(FirstId(1), FirstId(1));
	EXPECT_NE(FirstId(1), FirstId(2));
	EXPECT_TRUE(FirstId(1) < FirstId(2));
	EXPECT_EQ(FirstId().GetValue(), 0U);
	EXPECT_EQ(FirstId(42).GetValue(), 42U);
}

TEST(StrongTypeTest, WorksAsMapKey)
{
	std::map<FirstId, int> ordered{{FirstId(2), 20}, {FirstId(1), 10}};

	EXPECT_EQ(ordered.begin()->first, FirstId(1));
}
