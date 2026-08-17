//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/base/ovlibrary/test_string.cpp
//  Covers: ov::String
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/string.h>
#include <base/ovlibrary/data.h>

// ---------------------------------------------------------------------------
// Construction & basic access
// ---------------------------------------------------------------------------

TEST(OvString, DefaultConstructedIsEmpty)
{
	ov::String s;
	EXPECT_TRUE(s.IsEmpty());
	EXPECT_EQ(s.GetLength(), 0u);
	EXPECT_STREQ(s.CStr(), "");
}

TEST(OvString, ConstructFromCString)
{
	ov::String s("hello");
	EXPECT_EQ(s.GetLength(), 5u);
	EXPECT_STREQ(s.CStr(), "hello");
}

TEST(OvString, ConstructFromCStringWithLength)
{
	ov::String s("hello world", 5);
	EXPECT_EQ(s.GetLength(), 5u);
	EXPECT_STREQ(s.CStr(), "hello");
}

TEST(OvString, CopyConstructor)
{
	ov::String a("copy me");
	ov::String b(a);
	EXPECT_EQ(a, b);
	EXPECT_NE(a.CStr(), b.CStr());  // deep copy - different pointer
}

TEST(OvString, MoveConstructor)
{
	ov::String a("move me");
	const char *original_ptr = a.CStr();
	ov::String b(std::move(a));
	EXPECT_STREQ(b.CStr(), "move me");
	EXPECT_TRUE(a.IsEmpty());
	(void)original_ptr;
}

TEST(OvString, AssignFromCString)
{
	ov::String s;
	s = "assigned";
	EXPECT_STREQ(s.CStr(), "assigned");
}

// ---------------------------------------------------------------------------
// Append & Prepend
// ---------------------------------------------------------------------------

TEST(OvString, AppendChar)
{
	ov::String s("ab");
	s.Append('c');
	EXPECT_STREQ(s.CStr(), "abc");
}

TEST(OvString, AppendCString)
{
	ov::String s("foo");
	s.Append("bar");
	EXPECT_STREQ(s.CStr(), "foobar");
}

TEST(OvString, AppendCStringWithLength)
{
	ov::String s("foo");
	s.Append("barXXX", 3);
	EXPECT_STREQ(s.CStr(), "foobar");
}

TEST(OvString, PrependChar)
{
	ov::String s("bc");
	s.Prepend('a');
	EXPECT_STREQ(s.CStr(), "abc");
}

TEST(OvString, PrependCString)
{
	ov::String s("world");
	s.Prepend("hello ");
	EXPECT_STREQ(s.CStr(), "hello world");
}

TEST(OvString, AppendFormat)
{
	ov::String s("value=");
	s.AppendFormat("%d", 42);
	EXPECT_STREQ(s.CStr(), "value=42");
}

TEST(OvString, FormatString)
{
	ov::String s = ov::String::FormatString("x=%d y=%s", 7, "hi");
	EXPECT_STREQ(s.CStr(), "x=7 y=hi");
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

TEST(OvString, EqualityOperatorWithSameString)
{
	ov::String a("test");
	ov::String b("test");
	EXPECT_TRUE(a == b);
}

TEST(OvString, EqualityOperatorWithCString)
{
	ov::String s("test");
	EXPECT_TRUE(s == "test");
	EXPECT_FALSE(s == "other");
}

TEST(OvString, InequalityOperator)
{
	ov::String s("hello");
	EXPECT_TRUE(s != "world");
}

TEST(OvString, LessThanOperator)
{
	ov::String a("apple");
	ov::String b("banana");
	EXPECT_TRUE(a < b);
	EXPECT_FALSE(b < a);
}

// ---------------------------------------------------------------------------
// Search & IndexOf
// ---------------------------------------------------------------------------

TEST(OvString, IndexOfChar)
{
	ov::String s("hello");
	EXPECT_EQ(s.IndexOf('l'), 2);
	EXPECT_EQ(s.IndexOf('z'), -1);
}

TEST(OvString, IndexOfCharFromPosition)
{
	ov::String s("banana");
	EXPECT_EQ(s.IndexOf('a', 2), 3);
}

TEST(OvString, IndexOfCString)
{
	ov::String s("hello world");
	EXPECT_EQ(s.IndexOf("world"), 6);
	EXPECT_EQ(s.IndexOf("xyz"), -1);
}

TEST(OvString, IndexOfRev)
{
	ov::String s("abcabc");
	EXPECT_EQ(s.IndexOfRev('c'), 5);
}

// ---------------------------------------------------------------------------
// Case conversion
// ---------------------------------------------------------------------------

TEST(OvString, UpperCaseString)
{
	ov::String s("Hello World");
	auto upper = s.UpperCaseString();
	EXPECT_STREQ(upper.CStr(), "HELLO WORLD");
}

TEST(OvString, LowerCaseString)
{
	ov::String s("Hello World");
	auto lower = s.LowerCaseString();
	EXPECT_STREQ(lower.CStr(), "hello world");
}

TEST(OvString, MakeUpperInPlace)
{
	ov::String s("hello");
	s.MakeUpper();
	EXPECT_STREQ(s.CStr(), "HELLO");
}

TEST(OvString, MakeLowerInPlace)
{
	ov::String s("HELLO");
	s.MakeLower();
	EXPECT_STREQ(s.CStr(), "hello");
}

// ---------------------------------------------------------------------------
// Substring, Trim, Replace
// ---------------------------------------------------------------------------

TEST(OvString, SubstringFromStart)
{
	ov::String s("hello world");
	auto sub = s.Substring(6);
	EXPECT_STREQ(sub.CStr(), "world");
}

TEST(OvString, SubstringWithLength)
{
	ov::String s("hello world");
	auto sub = s.Substring(6, 3);
	EXPECT_STREQ(sub.CStr(), "wor");
}

TEST(OvString, TrimWhitespace)
{
	ov::String s("  hello  ");
	auto trimmed = s.Trim();
	EXPECT_STREQ(trimmed.CStr(), "hello");
}

TEST(OvString, TrimEmptyString)
{
	ov::String s("   ");
	auto trimmed = s.Trim();
	EXPECT_TRUE(trimmed.IsEmpty());
}

TEST(OvString, ReplaceToken)
{
	ov::String s("foo bar foo");
	auto replaced = s.Replace("foo", "baz");
	// Replace replaces first occurrence
	EXPECT_NE(replaced.IndexOf("baz"), -1);
}

// ---------------------------------------------------------------------------
// Split & Join
// ---------------------------------------------------------------------------

TEST(OvString, SplitBySeparator)
{
	auto parts = ov::String::Split("a,b,c", ",");
	ASSERT_EQ(parts.size(), 3u);
	EXPECT_EQ(parts[0], "a");
	EXPECT_EQ(parts[1], "b");
	EXPECT_EQ(parts[2], "c");
}

TEST(OvString, SplitEmptyString)
{
	auto parts = ov::String::Split("", ",");
	EXPECT_TRUE(parts.empty() || (parts.size() == 1 && parts[0].IsEmpty()));
}

TEST(OvString, SplitWithMaxCount)
{
	// With max_count=2, Split stops after the first 2 elements.
	// The second element contains the remainder of the string ("b,c,d").
	auto parts = ov::String::Split("a,b,c,d", ",", 2);
	ASSERT_EQ(parts.size(), 2u);
	EXPECT_EQ(parts[0], "a");
	EXPECT_EQ(parts[1], "b,c,d");
}

TEST(OvString, JoinList)
{
	std::vector<ov::String> parts = {"a", "b", "c"};
	auto joined = ov::String::Join(parts, ",");
	EXPECT_STREQ(joined.CStr(), "a,b,c");
}

TEST(OvString, JoinEmpty)
{
	std::vector<ov::String> parts;
	auto joined = ov::String::Join(parts, ",");
	EXPECT_TRUE(joined.IsEmpty());
}

// ---------------------------------------------------------------------------
// HasPrefix / HasSuffix
// ---------------------------------------------------------------------------

TEST(OvString, HasPrefix)
{
	ov::String s("hello world");
	EXPECT_TRUE(s.HasPrefix("hello"));
	EXPECT_FALSE(s.HasPrefix("world"));
	EXPECT_TRUE(s.HasPrefix('h'));
}

TEST(OvString, HasSuffix)
{
	ov::String s("hello world");
	EXPECT_TRUE(s.HasSuffix("world"));
	EXPECT_FALSE(s.HasSuffix("hello"));
	EXPECT_TRUE(s.HasSuffix('d'));
}

// ---------------------------------------------------------------------------
// Left / Right
// ---------------------------------------------------------------------------

TEST(OvString, Left)
{
	ov::String s("hello");
	EXPECT_STREQ(s.Left(3).CStr(), "hel");
}

TEST(OvString, Right)
{
	ov::String s("hello");
	EXPECT_STREQ(s.Right(3).CStr(), "llo");
}

TEST(OvString, LeftLargerThanLength)
{
	ov::String s("hi");
	EXPECT_STREQ(s.Left(100).CStr(), "hi");
}

// ---------------------------------------------------------------------------
// IsNumeric
// ---------------------------------------------------------------------------

TEST(OvString, IsNumeric)
{
	ov::String num("12345");
	EXPECT_TRUE(num.IsNumeric());

	ov::String notNum("123a5");
	EXPECT_FALSE(notNum.IsNumeric());
}

// ---------------------------------------------------------------------------
// Repeat
// ---------------------------------------------------------------------------

TEST(OvString, Repeat)
{
	auto s = ov::String::Repeat("ab", 3);
	EXPECT_STREQ(s.CStr(), "ababab");
}

TEST(OvString, RepeatZero)
{
	auto s = ov::String::Repeat("ab", 0);
	EXPECT_TRUE(s.IsEmpty());
}

// ---------------------------------------------------------------------------
// PadLeft / PadRight
// ---------------------------------------------------------------------------

TEST(OvString, PadLeft)
{
	auto s = ov::String("hi").PadLeftString(5);
	EXPECT_EQ(s.GetLength(), 5u);
	EXPECT_TRUE(s.HasSuffix("hi"));
}

TEST(OvString, PadRight)
{
	auto s = ov::String("hi").PadRightString(5);
	EXPECT_EQ(s.GetLength(), 5u);
	EXPECT_TRUE(s.HasPrefix("hi"));
}

// ---------------------------------------------------------------------------
// operators + and +=
// ---------------------------------------------------------------------------

TEST(OvString, ConcatenateOperator)
{
	ov::String a("foo");
	ov::String b("bar");
	ov::String c = a + b;
	EXPECT_STREQ(c.CStr(), "foobar");
}

TEST(OvString, AppendAssignOperator)
{
	ov::String s("foo");
	s += "bar";
	EXPECT_STREQ(s.CStr(), "foobar");
}

// ---------------------------------------------------------------------------
// Clear and SetLength
// ---------------------------------------------------------------------------

TEST(OvString, Clear)
{
	ov::String s("hello");
	s.Clear();
	EXPECT_TRUE(s.IsEmpty());
}

TEST(OvString, SetLengthShorter)
{
	ov::String s("hello");
	s.SetLength(3);
	EXPECT_EQ(s.GetLength(), 3u);
	EXPECT_STREQ(s.CStr(), "hel");
}

// ---------------------------------------------------------------------------
// ToData
// ---------------------------------------------------------------------------

TEST(OvString, ToDataIncludesNull)
{
	ov::String s("hi");
	auto data = s.ToData(true);
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->GetLength(), 3u);  // 'h' 'i' '\0'
}

TEST(OvString, ToDataExcludesNull)
{
	ov::String s("hi");
	auto data = s.ToData(false);
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->GetLength(), 2u);
}
