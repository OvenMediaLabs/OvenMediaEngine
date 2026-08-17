//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

//--------------------------------------------------------------------
// Related to FFmpeg
//--------------------------------------------------------------------
const char *GetFFmpegConfiguration();
const char *GetFFmpegVersion();
const char *GetFFmpegAvFormatVersion();
const char *GetFFmpegAvCodecVersion();
const char *GetFFmpegAvUtilVersion();
const char *GetFFmpegAvFilterVersion();
const char *GetFFmpegSwResampleVersion();
const char *GetFFmpegSwScaleVersion();
std::shared_ptr<ov::Error> InitializeFFmpeg();
std::shared_ptr<ov::Error> TerminateFFmpeg();

//--------------------------------------------------------------------
// Related to SRTP
//--------------------------------------------------------------------
const char *GetSrtpVersion();
std::shared_ptr<ov::Error> InitializeSrtp();
std::shared_ptr<ov::Error> TerminateSrtp();

//--------------------------------------------------------------------
// Related to SRT
//--------------------------------------------------------------------
const char *GetSrtVersion();
std::shared_ptr<ov::Error> InitializeSrt();
std::shared_ptr<ov::Error> TerminateSrt();

//--------------------------------------------------------------------
// Related to OpenSSL
//--------------------------------------------------------------------
const char *GetOpenSslConfiguration();
const char *GetOpenSslVersion();
std::shared_ptr<ov::Error> InitializeOpenSsl();
std::shared_ptr<ov::Error> TerminateOpenSsl();

//--------------------------------------------------------------------
// Related to AOM (AV1)
//--------------------------------------------------------------------
const char *GetAomVersion();

//--------------------------------------------------------------------
// Related to JsonCpp
//--------------------------------------------------------------------
const char *GetJsonCppVersion();

//--------------------------------------------------------------------
// Related to jemalloc
//--------------------------------------------------------------------
const char *GetJemallocVersion();
// NOTE: These APIs are expected to be called from a safe synchronous context
// such as the dedicated signal monitor thread in `signals.cpp`.
std::shared_ptr<ov::Error> InitializeJemalloc();
std::shared_ptr<ov::Error> TerminateJemalloc();

// By default, `jemalloc` is enabled in release builds and disabled in debug builds, so this API
// does nothing in debug builds unless you enable it explicitly.
// To enable it regardless of the build type, configure CMake with `-DOME_ENABLE_JEMALLOC=ON`.
// Once jemalloc is available, CMake defines the `OME_USE_JEMALLOC` compile definition.
// See `cmake/README.md#build-options` for details.
bool JemallocShowStats();

// `JemallocTriggerDump()` works only when jemalloc is built with heap profiling enabled.
// Configure CMake with `-DOME_USE_JEMALLOC_PROFILE=ON` (which requires `-DOME_ENABLE_JEMALLOC=ON`):
// once jemalloc is available, this defines the `OME_USE_JEMALLOC_PROFILE` compile definition and
// builds jemalloc with `--enable-prof` when it is (re)installed. See `cmake/README.md#build-options` for details.
//
// Without heap profiling enabled, this function does nothing and returns `false`.
bool JemallocTriggerDump();

//--------------------------------------------------------------------
// Related to spdlog
//--------------------------------------------------------------------
const char *GetSpdlogVersion();

//--------------------------------------------------------------------
// Related to whisper.cpp
//--------------------------------------------------------------------
std::shared_ptr<ov::Error> InitializeWhisper();
const char *GetWhisperCppVersion();
const char *GetGgmlVersion();