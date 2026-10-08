#include <gtest/gtest.h>

#include "webrtc_application.h"

using pvd::WebRTCApplication;

TEST(WebRTCStartBitrateHint, AppendsToExistingFmtp)
{
	auto fmtp = WebRTCApplication::ApplyStartBitrateHint("packetization-mode=1;profile-level-id=42e01f", 1000);
	EXPECT_STREQ(fmtp.CStr(), "packetization-mode=1;profile-level-id=42e01f;x-google-start-bitrate=1000");
}

TEST(WebRTCStartBitrateHint, EmptyFmtpCarriesOnlyTheHint)
{
	auto fmtp = WebRTCApplication::ApplyStartBitrateHint("", 1000);
	EXPECT_STREQ(fmtp.CStr(), "x-google-start-bitrate=1000");
}

TEST(WebRTCStartBitrateHint, ReplacesOfferedValue)
{
	auto fmtp = WebRTCApplication::ApplyStartBitrateHint("x-google-start-bitrate=300; packetization-mode=1", 1000);
	EXPECT_STREQ(fmtp.CStr(), "packetization-mode=1;x-google-start-bitrate=1000");
}
