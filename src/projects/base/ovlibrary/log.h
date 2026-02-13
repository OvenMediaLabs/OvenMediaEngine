//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./format_string.h"

#ifdef __cplusplus
extern "C"
{
#endif	// __cplusplus

	typedef enum OVLogLevel
	{
		// Used for logs that are intended to be shown only in debug builds.
		// Trace logs are completely disabled and never exposed in release builds.
		// This level is intended for highly detailed debugging information
		// that should not exist in production binaries.
		//
		// Examples:
		// - Per-byte packet tracing
		// - Detailed variable state changes within a loop
		// - Packet/data dump
		// - Function in/out points
		// - Memory allocation/deallocation points
		OVLogLevelTrace,

		// Debugging log left for application analysis
		//
		// Examples:
		// - Detailed information about connection establishment
		// - Detailed information about protocol operation
		OVLogLevelDebug,

		// Informational log that occurs throughout the application
		//
		// Examples:
		// - Application start/end points
		// - Server module start/end points
		OVLogLevelInformation,

		// Used when it is an error situation, but it does not affect the functional operation
		//
		// Examples:
		// - When a non-existent API is called from outside or the parameters are incorrect, and the function cannot be performed
		// - When attempting to reconnect due to a failed connection
		OVLogLevelWarning,

		// Used when some features of the application do not work at all
		//
		// Examples:
		// - When data cannot be recorded due to insufficient hard disk space
		// - When the TCP connection is lost due to a temporary network failure
		// - When the RTMP server module cannot be executed because 1935 port binding failed
		// - When the connection fails even after trying to reconnect sufficiently
		OVLogLevelError,

		// Used when the application can no longer be executed
		//
		// Examples:
		// - Impossible to execute due to incorrect essential environment settings
		// - When a crash occurs (called from SIG handler)
		// - When memory allocation fails
		// - When a thread cannot be created
		OVLogLevelCritical
	} OVLogLevel;

	typedef enum StatLogType
	{
		STAT_LOG_WEBRTC_EDGE_SESSION,
		STAT_LOG_WEBRTC_EDGE_REQUEST,
		STAT_LOG_WEBRTC_EDGE_VIEWERS,
		STAT_LOG_HLS_EDGE_SESSION,
		STAT_LOG_HLS_EDGE_REQUEST,
		STAT_LOG_HLS_EDGE_VIEWERS
	} StatLogType;

	// No operation
#define __ov_noop(...) \
	do                 \
	{                  \
	} while (false)

//--------------------------------------------------------------------
// Logging APIs
//--------------------------------------------------------------------
#if DEBUG
// `logp()` has the same level as `logt()`, but it is only evaluated when
// `ENABLE_VERBOSE_LOG` is defined, and ".Verbose" is automatically appended
// after the tag.
// To prevent `logp()` from being displayed when `ENABLE_VERBOSE_LOG` is defined,
// adjust the level of ".*\.Verbose" in `Logger.xml` appropriately.
//
// Example:
// ```
// <Tag name=".*\.Verbose" level="debug" />
// ```
#	ifdef ENABLE_VERBOSE_LOG
#		define logp(tag, format, ...) ov_log_internal(OVLogLevelTrace, tag ".Verbose", __FILE__, __LINE__, __PRETTY_FUNCTION__, format, ##__VA_ARGS__)
#	else  // ENABLE_VERBOSE_LOG
#		define logp __ov_noop
#	endif	// ENABLE_VERBOSE_LOG
#	define logt(tag, format, ...) ov_log_internal(OVLogLevelTrace, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, format, ##__VA_ARGS__)
#else  // DEBUG
	// `logp()` and `logt()` are no-ops in release builds.
#	define logp __ov_noop
#	define logt __ov_noop
#endif	// DEBUG

#define logd(tag, format, ...) ov_log_internal(OVLogLevelDebug, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, format, ##__VA_ARGS__)
#define logi(tag, format, ...) ov_log_internal(OVLogLevelInformation, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, format, ##__VA_ARGS__)
#define logw(tag, format, ...) ov_log_internal(OVLogLevelWarning, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, format, ##__VA_ARGS__)
#define loge(tag, format, ...) ov_log_internal(OVLogLevelError, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, format, ##__VA_ARGS__)
#define logc(tag, format, ...) ov_log_internal(OVLogLevelCritical, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, format, ##__VA_ARGS__)

//--------------------------------------------------------------------
// Logging APIs with tag
//--------------------------------------------------------------------
#define logtp(format, ...) logp(OV_LOG_TAG, format, ##__VA_ARGS__)
#define logtt(format, ...) logt(OV_LOG_TAG, format, ##__VA_ARGS__)
#define logtd(format, ...) logd(OV_LOG_TAG, format, ##__VA_ARGS__)
#define logti(format, ...) logi(OV_LOG_TAG, format, ##__VA_ARGS__)
#define logtw(format, ...) logw(OV_LOG_TAG, format, ##__VA_ARGS__)
#define logte(format, ...) loge(OV_LOG_TAG, format, ##__VA_ARGS__)
#define logtc(format, ...) logc(OV_LOG_TAG, format, ##__VA_ARGS__)

//--------------------------------------------------------------------
// Logging APIs with additional prefix
// (`OV_LOG_PREFIX_FORMAT` and `OV_LOG_PREFIX_VALUE` must be defined.)
//--------------------------------------------------------------------
#define logap(format, ...) logtp(OV_LOG_PREFIX_FORMAT format, OV_LOG_PREFIX_VALUE, ##__VA_ARGS__)
#define logat(format, ...) logtt(OV_LOG_PREFIX_FORMAT format, OV_LOG_PREFIX_VALUE, ##__VA_ARGS__)
#define logad(format, ...) logtd(OV_LOG_PREFIX_FORMAT format, OV_LOG_PREFIX_VALUE, ##__VA_ARGS__)
#define logai(format, ...) logti(OV_LOG_PREFIX_FORMAT format, OV_LOG_PREFIX_VALUE, ##__VA_ARGS__)
#define logaw(format, ...) logtw(OV_LOG_PREFIX_FORMAT format, OV_LOG_PREFIX_VALUE, ##__VA_ARGS__)
#define logae(format, ...) logte(OV_LOG_PREFIX_FORMAT format, OV_LOG_PREFIX_VALUE, ##__VA_ARGS__)
#define logac(format, ...) logtc(OV_LOG_PREFIX_FORMAT format, OV_LOG_PREFIX_VALUE, ##__VA_ARGS__)

#define stat_log(type, format, ...) ov_stat_log_internal(type, OVLogLevelInformation, "STAT", __FILE__, __LINE__, __PRETTY_FUNCTION__, format, ##__VA_ARGS__)

	/// Primary filter rule applied to all logs
	///
	/// @param level Only logs above this level are displayed
	void ov_log_set_level(OVLogLevel level);

	void ov_log_reset_enable();

	/// @param tag_regex tag pattern
	/// @param level Apply is_enable only for logs above this level. Logs outside this level are considered !is_enable
	/// @param is_enabled Whether enabled or not
	///
	/// @returns Whether it was applied successfully
	///
	/// @remarks Example of using regex: "App\..+" == "App.Hello", "App.World", "App.foo", "App.bar", ....
	///         (For regex, refer to http://www.cplusplus.com/reference/regex/ECMAScript)
	///         The first entered tag_regex has higher priority.
	///         If a different level is set for the same tag, the first set level is applied.
	///         e.g. 1) If level is debug and is_enabled is false, debug~critical logs are not printed.
	///         e.g. 2) If level is warning and is_enabled is false, warning~critical logs are not printed.
	///         e.g. 3) If level is warning and is_enabled is true, debug~information logs are not printed, warning~critical logs are printed.
	///         e.g. 4) If level is info and is_enabled is true, debug logs are not printed, information~critical logs are printed.
	bool ov_log_set_enable(const char *tag_regex, OVLogLevel level, bool is_enabled);
	bool ov_log_get_enabled(const char *tag, OVLogLevel level);

	void ov_log_internal(OVLogLevel level, const char *tag, const char *file, int line, const char *method, const char *format, ...) OV_PRINTF_FORMAT(6, 7);
	void ov_log_set_path(const char *log_path);
	const char *ov_log_get_path();

	void ov_stat_log_internal(StatLogType type, OVLogLevel level, const char *tag, const char *file, int line, const char *method, const char *format, ...);
	void ov_stat_log_set_path(StatLogType type, const char *log_path);

#ifdef __cplusplus
}
#endif	// __cplusplus
