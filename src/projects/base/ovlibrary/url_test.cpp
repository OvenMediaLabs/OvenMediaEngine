//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/base/ovlibrary/test_url.cpp
//  Covers: ov::Url
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/url.h>

// ---------------------------------------------------------------------------
// Parse - basic components
// ---------------------------------------------------------------------------

TEST(OvUrl, ParseFullUrl)
{
	auto url = ov::Url::Parse("rtmp://live.example.com:1935/app/stream");
	ASSERT_NE(url, nullptr);
	EXPECT_STREQ(url->Scheme().CStr(), "rtmp");
	EXPECT_STREQ(url->Host().CStr(), "live.example.com");
	EXPECT_EQ(url->Port(), 1935u);
	EXPECT_STREQ(url->App().CStr(), "app");
	EXPECT_STREQ(url->Stream().CStr(), "stream");
}

TEST(OvUrl, ParseHttpUrl)
{
	auto url = ov::Url::Parse("http://airensoft.com/path/to/resource");
	ASSERT_NE(url, nullptr);
	EXPECT_STREQ(url->Scheme().CStr(), "http");
	EXPECT_STREQ(url->Host().CStr(), "airensoft.com");
	EXPECT_STREQ(url->Path().CStr(), "/path/to/resource");
}

TEST(OvUrl, ParseUrlWithQueryString)
{
	auto url = ov::Url::Parse("https://host.com/app/stream?token=abc&quality=high");
	ASSERT_NE(url, nullptr);
	EXPECT_TRUE(url->HasQueryString());
	EXPECT_STREQ(url->GetQueryValue("token").CStr(), "abc");
	EXPECT_STREQ(url->GetQueryValue("quality").CStr(), "high");
}

TEST(OvUrl, ParseUrlMissingPort)
{
	auto url = ov::Url::Parse("rtsp://camera.local/live");
	ASSERT_NE(url, nullptr);
	EXPECT_STREQ(url->Scheme().CStr(), "rtsp");
	EXPECT_STREQ(url->Host().CStr(), "camera.local");
	EXPECT_EQ(url->Port(), 0u);
}

TEST(OvUrl, ParseUrlWithCredentials)
{
	auto url = ov::Url::Parse("rtmp://user:password@live.example.com:1935/app/stream");
	ASSERT_NE(url, nullptr);
	EXPECT_STREQ(url->Id().CStr(), "user");
	EXPECT_STREQ(url->Password().CStr(), "password");
	EXPECT_STREQ(url->Host().CStr(), "live.example.com");
}

TEST(OvUrl, ParseSrtUrl)
{
	auto url = ov::Url::Parse("srt://127.0.0.1:9999?streamid=app/stream");
	ASSERT_NE(url, nullptr);
	EXPECT_STREQ(url->Scheme().CStr(), "srt");
	EXPECT_EQ(url->Port(), 9999u);
}

TEST(OvUrl, ParseNullOrEmpty)
{
	auto url = ov::Url::Parse("");
	// Empty URL should fail gracefully
	EXPECT_EQ(url, nullptr);
}

// ---------------------------------------------------------------------------
// Encode / Decode
// ---------------------------------------------------------------------------

TEST(OvUrl, EncodeBasicChars)
{
	auto encoded = ov::Url::Encode("hello world");
	// space should be encoded as + or %20
	EXPECT_FALSE(ov::String(encoded).IndexOf("hello") == -1);
	EXPECT_EQ(ov::String(encoded).IndexOf(' '), -1);
}

TEST(OvUrl, EncodeSafeChars)
{
	auto encoded = ov::Url::Encode("abc123-_.~");
	EXPECT_STREQ(encoded.CStr(), "abc123-_.~");
}

TEST(OvUrl, EncodeSpecialChars)
{
	auto encoded = ov::Url::Encode("a=1&b=2");
	EXPECT_TRUE(ov::String(encoded).IndexOf('=') == -1);
	EXPECT_TRUE(ov::String(encoded).IndexOf('&') == -1);
}

// ov::Url::Encode uses '+' for space, but ov::Url::Decode only handles
// %XX sequences — it does NOT convert '+' back to space. This is by design.
TEST(OvUrl, DecodePercentEncodedSpace)
{
	auto decoded = ov::Url::Decode("hello%20world");
	EXPECT_STREQ(decoded.CStr(), "hello world");
}

TEST(OvUrl, DecodePlusRemainsPlus)
{
	// '+' is NOT decoded to space by ov::Url::Decode
	auto decoded = ov::Url::Decode("hello+world");
	EXPECT_STREQ(decoded.CStr(), "hello+world");
}

// ---------------------------------------------------------------------------
// Query string manipulation
// ---------------------------------------------------------------------------

TEST(OvUrl, HasQueryKey)
{
	auto url = ov::Url::Parse("https://host.com/stream?token=abc");
	ASSERT_NE(url, nullptr);
	EXPECT_TRUE(url->HasQueryKey("token"));
	EXPECT_FALSE(url->HasQueryKey("missing"));
}

TEST(OvUrl, PushBackQueryKey)
{
	auto url = ov::Url::Parse("https://host.com/app/stream");
	ASSERT_NE(url, nullptr);
	url->PushBackQueryKey("token", "xyz");
	EXPECT_TRUE(url->HasQueryString());
	EXPECT_STREQ(url->GetQueryValue("token").CStr(), "xyz");
}

TEST(OvUrl, RemoveQueryKey)
{
	auto url = ov::Url::Parse("https://host.com/app/stream?keep=yes&remove=no");
	ASSERT_NE(url, nullptr);
	url->RemoveQueryKey("remove");
	EXPECT_FALSE(url->HasQueryKey("remove"));
	EXPECT_TRUE(url->HasQueryKey("keep"));
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

TEST(OvUrl, SetPort)
{
	auto url = ov::Url::Parse("rtmp://host.com:1935/app/stream");
	ASSERT_NE(url, nullptr);
	url->SetPort(1936);
	EXPECT_EQ(url->Port(), 1936u);
}

TEST(OvUrl, SetApp)
{
	auto url = ov::Url::Parse("rtmp://host.com:1935/app/stream");
	ASSERT_NE(url, nullptr);
	url->SetApp("newapp");
	EXPECT_STREQ(url->App().CStr(), "newapp");
}

TEST(OvUrl, SetStream)
{
	auto url = ov::Url::Parse("rtmp://host.com:1935/app/stream");
	ASSERT_NE(url, nullptr);
	url->SetStream("newstream");
	EXPECT_STREQ(url->Stream().CStr(), "newstream");
}

// ---------------------------------------------------------------------------
// IsAbsolute
// ---------------------------------------------------------------------------

TEST(OvUrl, IsAbsoluteWithScheme)
{
	EXPECT_TRUE(ov::Url::IsAbsolute("http://example.com/path"));
	EXPECT_TRUE(ov::Url::IsAbsolute("rtmp://host:1935/app/stream"));
}

TEST(OvUrl, IsAbsoluteRelativePath)
{
	EXPECT_FALSE(ov::Url::IsAbsolute("/path/to/resource"));
	EXPECT_FALSE(ov::Url::IsAbsolute("path/to/resource"));
}

// ---------------------------------------------------------------------------
// Clone
// ---------------------------------------------------------------------------

TEST(OvUrl, Clone)
{
	auto url = ov::Url::Parse("rtmp://host.com:1935/app/stream?token=abc");
	ASSERT_NE(url, nullptr);
	auto clone = url->Clone();
	ASSERT_NE(clone, nullptr);
	EXPECT_NE(url.get(), clone.get());
	EXPECT_EQ(url->Scheme(), clone->Scheme());
	EXPECT_EQ(url->Host(), clone->Host());
	EXPECT_EQ(url->Port(), clone->Port());
	EXPECT_EQ(url->App(), clone->App());
	EXPECT_EQ(url->Stream(), clone->Stream());
	EXPECT_STREQ(url->GetQueryValue("token").CStr(), clone->GetQueryValue("token").CStr());
}

// ---------------------------------------------------------------------------
// ToUrlString
// ---------------------------------------------------------------------------

TEST(OvUrl, ToUrlStringRoundTrip)
{
	const char *original = "rtmp://live.example.com:1935/app/stream";
	auto url = ov::Url::Parse(original);
	ASSERT_NE(url, nullptr);
	auto reconstructed = url->ToUrlString(false);
	EXPECT_STREQ(reconstructed.CStr(), original);
}
