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
// Related to JsonCpp
//--------------------------------------------------------------------
const char *GetJsonCppVersion();

//--------------------------------------------------------------------
// Related to jemalloc
//--------------------------------------------------------------------
const char *GetJemallocVersion();
// NOTE: These APIs are not thread-safe and are assumed to be called from the main thread.
std::shared_ptr<ov::Error> InitializeJemalloc();
std::shared_ptr<ov::Error> TerminateJemalloc();

// By default, `jemalloc` is enabled only in release builds, and this API does not work in debug mode.
// If you want to enable it forcibly, comment out the following lines in `projects/main/AMS.mk`.
//
// ```
// # ifeq ($(MAKECMDGOALS),release)
// $(call add_pkg_config,jemalloc)
// LOCAL_CFLAGS += -DOME_USE_JEMALLOC
// LOCAL_CXXFLAGS += -DOME_USE_JEMALLOC
// # endif
// ```
bool JemallocShowStats();

// To use `JemallocTriggerDump()`,
//
// 1) `jemalloc` must be built with the `--enable-prof` option during the `configure` step.
// ```
// $ cd path/to/jemalloc
// $ ./autogen.sh && ./configure --enable-prof && make ...
// ```
// 2) Also, `OME_USE_JEMALLOC_PROFILE` must be defined in `projects/main/AMS.mk`.
// ```
// $(call add_pkg_config,jemalloc)
// LOCAL_CFLAGS += -DOME_USE_JEMALLOC -DOME_USE_JEMALLOC_PROFILE
// LOCAL_CXXFLAGS += -DOME_USE_JEMALLOC -DOME_USE_JEMALLOC_PROFILE
// ```
//
// Otherwise, this function returns `false`.
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