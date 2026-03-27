//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/base/ovlibrary/test_regex.cpp
//  Covers: ov::Regex, ov::MatchResult, ov::MatchGroup
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/regex.h>

// ---------------------------------------------------------------------------
// Basic compile & match
// ---------------------------------------------------------------------------

TEST(OvRegex, CompileAndMatchSimple)
{
	ov::Regex re("hello");
	auto err = re.Compile();
	EXPECT_EQ(err, nullptr) << (err ? err->What() : "");

	auto result = re.Matches("say hello world");
	EXPECT_TRUE(result.IsMatched());
}

TEST(OvRegex, NoMatchReturnsIsMatchedFalse)
{
	ov::Regex re("xyz");
	re.Compile();
	auto result = re.Matches("hello world");
	EXPECT_FALSE(result.IsMatched());
}

TEST(OvRegex, CompileInvalidPatternReturnsError)
{
	ov::Regex re("(unclosed");
	auto err = re.Compile();
	EXPECT_NE(err, nullptr);
}

// ---------------------------------------------------------------------------
// Capture groups
// ---------------------------------------------------------------------------

TEST(OvRegex, CaptureGroup)
{
	ov::Regex re("(\\w+)/(\\w+)");
	re.Compile();
	auto result = re.Matches("app/stream");
	ASSERT_TRUE(result.IsMatched());
	EXPECT_GE(result.GetGroupCount(), 3u);  // group 0 = full, 1 = app, 2 = stream
	EXPECT_EQ(result.GetGroupAt(1).GetValue(), "app");
	EXPECT_EQ(result.GetGroupAt(2).GetValue(), "stream");
}

TEST(OvRegex, NamedCaptureGroup)
{
	ov::Regex re("(?<app>[^/]+)/(?<stream>.+)");
	re.Compile();
	auto result = re.Matches("live/test-stream");
	ASSERT_TRUE(result.IsMatched());

	auto app_group = result.GetNamedGroup("app");
	auto stream_group = result.GetNamedGroup("stream");
	EXPECT_TRUE(app_group.IsValid());
	EXPECT_TRUE(stream_group.IsValid());
	EXPECT_EQ(app_group.GetValue(), "live");
	EXPECT_EQ(stream_group.GetValue(), "test-stream");
}

TEST(OvRegex, NamedGroupNotFoundIsInvalid)
{
	ov::Regex re("(?<token>[a-z]+)");
	re.Compile();
	auto result = re.Matches("abc");
	ASSERT_TRUE(result.IsMatched());
	auto missing = result.GetNamedGroup("nonexistent");
	EXPECT_FALSE(missing.IsValid());
}

// ---------------------------------------------------------------------------
// CompiledRegex static constructor
// ---------------------------------------------------------------------------

TEST(OvRegex, CompiledRegexHelperWorks)
{
	auto re = ov::Regex::CompiledRegex("\\d+");
	auto result = re.Matches("value=42");
	EXPECT_TRUE(result.IsMatched());
}

// ---------------------------------------------------------------------------
// WildCardRegex
// ---------------------------------------------------------------------------

TEST(OvRegex, WildCardRegexMatchesStar)
{
	auto pattern = ov::Regex::WildCardRegex("*.example.com");
	ov::Regex re(pattern.CStr());
	re.Compile();
	EXPECT_TRUE(re.Matches("sub.example.com").IsMatched());
	EXPECT_TRUE(re.Matches("other.example.com").IsMatched());
}

TEST(OvRegex, WildCardRegexExactMatch)
{
	auto pattern = ov::Regex::WildCardRegex("host.example.com", true);
	ov::Regex re(pattern.CStr());
	re.Compile();
	EXPECT_TRUE(re.Matches("host.example.com").IsMatched());
	EXPECT_FALSE(re.Matches("other.example.com").IsMatched());
	EXPECT_FALSE(re.Matches("prefix.host.example.com").IsMatched());
}

TEST(OvRegex, WildCardRegexEscapesSpecialChars)
{
	// Dots should be literal, not regex "any char"
	auto pattern = ov::Regex::WildCardRegex("a.b.c", true);
	ov::Regex re(pattern.CStr());
	re.Compile();
	EXPECT_TRUE(re.Matches("a.b.c").IsMatched());
	EXPECT_FALSE(re.Matches("aXbXc").IsMatched());
}

// ---------------------------------------------------------------------------
// Case-insensitive option
// ---------------------------------------------------------------------------

TEST(OvRegex, CaseInsensitiveOption)
{
	ov::Regex re("hello", ov::Regex::Option::CaseInsensitive);
	re.Compile();
	EXPECT_TRUE(re.Matches("HELLO world").IsMatched());
	EXPECT_TRUE(re.Matches("Hello World").IsMatched());
}

// ---------------------------------------------------------------------------
// MatchGroup offsets
// ---------------------------------------------------------------------------

TEST(OvRegex, MatchGroupOffsets)
{
	ov::Regex re("(world)");
	re.Compile();
	auto result = re.Matches("hello world");
	ASSERT_TRUE(result.IsMatched());
	auto g = result.GetGroupAt(1);
	EXPECT_EQ(g.GetStartOffset(), 6u);
	EXPECT_EQ(g.GetEndOffset(), 11u);
	EXPECT_EQ(g.GetLength(), 5u);
}

// ---------------------------------------------------------------------------
// Copy & move
// ---------------------------------------------------------------------------

TEST(OvRegex, CopyConstructedRegexWorks)
{
	ov::Regex re("\\d+");
	re.Compile();
	ov::Regex copy(re);
	auto result = copy.Matches("42");
	EXPECT_TRUE(result.IsMatched());
}

TEST(OvRegex, MoveConstructedRegexWorks)
{
	ov::Regex re("\\d+");
	re.Compile();
	ov::Regex moved(std::move(re));
	EXPECT_TRUE(moved.Matches("99").IsMatched());
}
