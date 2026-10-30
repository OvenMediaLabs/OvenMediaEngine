#pragma once

#define OV_LOG_TAG "LLHLS Publisher"

constexpr bool kDefaultHlsLegacy = false;
constexpr bool kDefaultHlsRewind = true;

// How long to wait before asking for a key again after a failed request, so that a
// source that is failing is not asked once per segment
constexpr int64_t kKeyRequestRetryIntervalMs = 5000;

// A player may be between connections for a moment, so a session that lost its last
// connection waits this long before it closes
constexpr int kSessionCloseGraceMs = 3000;

// Length of the random key in a session value, <session id>_<key>
constexpr size_t kSessionKeyLength = 8;