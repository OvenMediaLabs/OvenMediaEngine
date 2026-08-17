#pragma once

#define OV_LOG_TAG "LLHLS Publisher"

constexpr bool kDefaultHlsLegacy = false;
constexpr bool kDefaultHlsRewind = true;

// How long to wait before asking for a key again after a failed request, so that a
// source that is failing is not asked once per segment
constexpr int64_t kKeyRequestRetryIntervalMs = 5000;