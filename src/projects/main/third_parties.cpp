//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./third_parties.h"

#include <base/ovcrypto/ovcrypto.h>
#include <base/ovsocket/ovsocket.h>
#include <srt/srt.h>
#ifdef OME_USE_JEMALLOC
#	include <jemalloc/jemalloc.h>
#endif	// OME_USE_JEMALLOC

#include <spdlog/spdlog.h>

#include <regex>

//--------------------------------------------------------------------
// Related to FFmpeg
//--------------------------------------------------------------------
extern "C"
{
#include <libavcodec/version.h>
#include <libavfilter/version.h>
#include <libavformat/avformat.h>
#include <libavformat/version.h>
#include <libavutil/ffversion.h>
#include <libavutil/version.h>
#include <libswresample/version.h>
#include <libswscale/version.h>
}

const char *GetFFmpegConfiguration()
{
	return avutil_configuration();
}

const char *GetFFmpegVersion()
{
	return FFMPEG_VERSION;
}

const char *GetFFmpegAvFormatVersion()
{
	return AV_STRINGIFY(LIBAVFORMAT_VERSION);
}

const char *GetFFmpegAvCodecVersion()
{
	return AV_STRINGIFY(LIBAVCODEC_VERSION);
}

const char *GetFFmpegAvUtilVersion()
{
	return AV_STRINGIFY(LIBAVUTIL_VERSION);
}

const char *GetFFmpegAvFilterVersion()
{
	return AV_STRINGIFY(LIBAVFILTER_VERSION);
}

const char *GetFFmpegSwResampleVersion()
{
	return AV_STRINGIFY(LIBSWRESAMPLE_VERSION);
}

const char *GetFFmpegSwScaleVersion()
{
	return AV_STRINGIFY(LIBSWSCALE_VERSION);
}

static void OnFFmpegLog(void *avcl, int level, const char *fmt, va_list args)
{
	static const char *FFMPEG_LOG_TAG = "FFmpeg";
	AVClass *clazz					  = nullptr;
	ov::String message;

	if (avcl != nullptr)
	{
		clazz = *(static_cast<AVClass **>(avcl));

		if (clazz != nullptr)
		{
			message.AppendFormat("[%s: %p] ", clazz->class_name, avcl);
		}
	}

	ov::String format(fmt);

	if (format.HasSuffix("\n"))
	{
		// Remove new line character
		format.SetLength(format.GetLength() - 1);
	}

	message.AppendVFormat(format, args);

	switch (level)
	{
		case AV_LOG_QUIET:
			break;

		case AV_LOG_PANIC:
		case AV_LOG_FATAL:
			::ov_log_internal(OVLogLevelCritical, FFMPEG_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;
		case AV_LOG_ERROR:
			::ov_log_internal(OVLogLevelError, FFMPEG_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;

		case AV_LOG_WARNING:
			::ov_log_internal(OVLogLevelWarning, FFMPEG_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;

		case AV_LOG_INFO:
			::ov_log_internal(OVLogLevelInformation, FFMPEG_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;

		case AV_LOG_VERBOSE:
		case AV_LOG_DEBUG:
		default:
			::ov_log_internal(OVLogLevelDebug, FFMPEG_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;
	}
}

std::shared_ptr<ov::Error> InitializeFFmpeg()
{
	::av_log_set_callback(OnFFmpegLog);
#if DEBUG
	::av_log_set_level(AV_LOG_DEBUG);
#else	// DEBUG
	::av_log_set_level(AV_LOG_INFO);
#endif	// DEBUG
	::avformat_network_init();

	return nullptr;
}

std::shared_ptr<ov::Error> TerminateFFmpeg()
{
	return nullptr;
}

//--------------------------------------------------------------------
// Related to SRTP
//--------------------------------------------------------------------
#include <srtp2/srtp.h>

const char *GetSrtpVersion()
{
	return srtp_get_version_string();
}

std::shared_ptr<ov::Error> InitializeSrtp()
{
	int err = ::srtp_init();

	if (err != srtp_err_status_ok)
	{
		return ov::Error::CreateError("SRTP", "Could not initialize SRTP (err: %d)", err);
	}

	return nullptr;
}

std::shared_ptr<ov::Error> TerminateSrtp()
{
	int err = ::srtp_shutdown();

	if (err != srtp_err_status_ok)
	{
		return ov::Error::CreateError("SRTP", "Could not uninitialize SRTP (err: %d)", err);
	}

	return nullptr;
}

//--------------------------------------------------------------------
// Related to SRT
//--------------------------------------------------------------------
const char *GetSrtVersion()
{
	return SRT_VERSION_STRING;
}

static void SrtLogHandler(void *opaque, int level, const char *file, int line, const char *area, const char *message)
{
	// SRT log format:
	// HH:mm:ss.ssssss/SRT:xxxx:xxxxxx.N: xxx.c: .................
	// 13:20:15.618019.N: SRT.c: PASSING request from: 192.168.0.212:63308 to agent:397692317
	// 13:20:15.618019.!!FATAL!!: SRT.c: PASSING request from: 192.168.0.212:63308 to agent:397692317
	// 13:20:15.618019.!W: SRT.c: PASSING request from: 192.168.0.212:63308 to agent:397692317
	// 13:20:15.618019.*E: SRT.c: PASSING request from: 192.168.0.212:63308 to agent:397692317
	// 20:41:11.929158/ome_origin*E: SRT.d: SND-DROPPED 1 packets - lost delaying for 1021m
	// 13:20:15.618019/SRT:RcvQ:worker.N: SRT.c: PASSING request from: 192.168.0.212:63308 to agent:397692317

	std::smatch matches;
	std::string m = message;
	ov::String mess;

	if (std::regex_search(m, matches, std::regex("^([0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{6})([\\/a-zA-Z:.!*_0-9]+) ([\\/a-zA-Z:.0-9!]+ )?(.+)")))
	{
		mess = std::string(matches[4]).c_str();
	}
	else
	{
		// Unknown pattern
		mess = message;
	}

	// Suppress the following message:
	//   12:34:56.012345/xxxxx*E:SRT.cn: srt_accept: no pending connection available at the moment
	//
	// When `SRT_EPOLL_ET` is enabled and clients connect simultaneously,
	// `srt_accept()` must be called multiple times to accommodate the client's connections.
	// In this normal scenario, there is no way to avoid the error log below.
	//
	// It seems that the SRT developers consider this to be normal behavior.
	// (https://github.com/Haivision/srt/discussions/2768)
	if (mess.IndexOf("srt_accept: no pending connection available at the moment") >= 0)
	{
		// Ignore this message
		return;
	}

	const char *SRT_LOG_TAG = "SRT";
	ov::String new_file		= file;
	new_file.Append("@SRT");

	switch (level)
	{
		case srt_logging::LogLevel::debug:
			::ov_log_internal(OVLogLevelDebug, SRT_LOG_TAG, new_file, line, area, "%s", mess.CStr());
			break;

		case srt_logging::LogLevel::note:
			::ov_log_internal(OVLogLevelInformation, SRT_LOG_TAG, new_file, line, area, "%s", mess.CStr());
			break;

		case srt_logging::LogLevel::warning:
			::ov_log_internal(OVLogLevelWarning, SRT_LOG_TAG, new_file, line, area, "%s", mess.CStr());
			break;

		case srt_logging::LogLevel::error:
			::ov_log_internal(OVLogLevelError, SRT_LOG_TAG, new_file, line, area, "%s", mess.CStr());
			break;

		case srt_logging::LogLevel::fatal:
			::ov_log_internal(OVLogLevelCritical, SRT_LOG_TAG, new_file, line, area, "%s", mess.CStr());
			break;

		default:
			::ov_log_internal(OVLogLevelError, SRT_LOG_TAG, new_file, line, area, "(Unknown level: %d) %s", level, mess.CStr());
			break;
	}
}

std::shared_ptr<ov::Error> InitializeSrt()
{
	// https://github.com/Haivision/srt/blob/master/docs/API-functions.md#srt_startup
	// 0 = successfully run, or already started
	// 1 = this is the first startup, but the GC thread is already running
	// -1 = failed
	if (::srt_startup() == -1)
	{
		return ov::SrtError::CreateErrorFromSrt();
	}

#if DEBUG
	::srt_setloglevel(srt_logging::LogLevel::debug);
#else	// DEBUG
	::srt_setloglevel(srt_logging::LogLevel::note);
#endif	// DEBUG
	::srt_setloghandler(nullptr, SrtLogHandler);

	return nullptr;
}

std::shared_ptr<ov::Error> TerminateSrt()
{
	// https://github.com/Haivision/srt/blob/master/docs/API-functions.md#srt_cleanup
	// 0 (A possibility to return other values is reserved for future use)
	if (::srt_cleanup() != 0)
	{
		return ov::SrtError::CreateErrorFromSrt();
	}

	return nullptr;
}

//--------------------------------------------------------------------
// Related to OpenSSL
//--------------------------------------------------------------------
const char *GetOpenSslConfiguration()
{
	return OpenSSL_version(OPENSSL_CFLAGS);
}

const char *GetOpenSslVersion()
{
	return OpenSSL_version(OPENSSL_VERSION);
}

std::shared_ptr<ov::Error> InitializeOpenSsl()
{
	if (ov::OpensslManager::GetInstance()->InitializeOpenssl())
	{
		return nullptr;
	}

	return std::make_shared<ov::OpensslError>();
}

std::shared_ptr<ov::Error> TerminateOpenSsl()
{
	if (ov::OpensslManager::GetInstance()->ReleaseOpenSSL())
	{
		return nullptr;
	}

	return std::make_shared<ov::OpensslError>();
}

const char *GetJsonCppVersion()
{
	return JSONCPP_VERSION_STRING;
}

const char *GetJemallocVersion()
{
#ifdef OME_USE_JEMALLOC
	return JEMALLOC_VERSION;
#else	// OME_USE_JEMALLOC
	return "(disabled)";
#endif	// OME_USE_JEMALLOC
}

namespace third_party::internal
{
	static constexpr const char *JEMALLOC_LOG_TAG = "Jemalloc";

#ifdef OME_USE_JEMALLOC
	class JemallocHelper
	{
	public:
		void Initialize()
		{
#	ifdef OME_USE_JEMALLOC_PROFILE
			logw(JEMALLOC_LOG_TAG, "Jemalloc profiling is enabled - this may slow down the performance.");

			_is_running = true;

			_thread		= std::thread([this]() {
				while (_is_running.load())
				{
					if (_trigger_event.Wait())
					{
						_trigger_event.Reset();

						if (_is_running.load())
						{
							int result = ::mallctl("prof.dump", nullptr, nullptr, nullptr, 0);

							if (result == 0)
							{
								logi(JEMALLOC_LOG_TAG, "Jemalloc profile dumped successfully.");
							}
							else
							{
								loge(JEMALLOC_LOG_TAG, "Could not dump jemalloc profile (err: %d)", result);
							}
						}
					}
				}
			});
#	endif	// OME_USE_JEMALLOC_PROFILE
		}

		void Terminate()
		{
#	ifdef OME_USE_JEMALLOC_PROFILE
			_is_running.store(false);
			_trigger_event.SetEvent();
			if (_thread.joinable())
			{
				_thread.join();
			}
#	endif	// OME_USE_JEMALLOC_PROFILE
		}

		void ShowStats()
		{
			::malloc_stats_print(WriteCallback, nullptr, nullptr);
		}

		void Dump()
		{
#	ifdef OME_USE_JEMALLOC_PROFILE
			_trigger_event.SetEvent();
#	endif	// OME_USE_JEMALLOC_PROFILE
		}

	private:
		static void WriteCallback(void *paque, const char *buf)
		{
			logi(JEMALLOC_LOG_TAG, "%s", buf);
		}

	private:
		std::atomic<bool> _is_running{true};
		ov::Event _trigger_event{true};
		std::thread _thread;
	};

	static std::shared_ptr<JemallocHelper> g_jemalloc_helper;
#else  // OME_USE_JEMALLOC
#	ifdef OME_USE_JEMALLOC_PROFILE
#		error "`OME_USE_JEMALLOC_PROFILE` requires `OME_USE_JEMALLOC`"
#	endif	// OME_USE_JEMALLOC_PROFILE
#endif		// OME_USE_JEMALLOC
}  // namespace third_party::internal

std::shared_ptr<ov::Error> InitializeJemalloc()
{
#ifdef OME_USE_JEMALLOC
	if (third_party::internal::g_jemalloc_helper == nullptr)
	{
		auto helper = std::make_shared<third_party::internal::JemallocHelper>();
		helper->Initialize();

		third_party::internal::g_jemalloc_helper = helper;
	}
	else
	{
		OV_ASSERT2(false);
	}
#endif	// OME_USE_JEMALLOC

	return nullptr;
}

std::shared_ptr<ov::Error> TerminateJemalloc()
{
#ifdef OME_USE_JEMALLOC
	auto helper = std::move(third_party::internal::g_jemalloc_helper);

	if (helper != nullptr)
	{
		helper->Terminate();
	}
	else
	{
		OV_ASSERT2(false);
	}
#endif	// OME_USE_JEMALLOC

	return nullptr;
}

bool JemallocShowStats()
{
#ifdef OME_USE_JEMALLOC
	if (third_party::internal::g_jemalloc_helper == nullptr)
	{
		OV_ASSERT2(false);
		return false;
	}

	third_party::internal::g_jemalloc_helper->ShowStats();
	return true;
#else	// OME_USE_JEMALLOC
	return false;
#endif	// OME_USE_JEMALLOC
}

bool JemallocTriggerDump()
{
#ifdef OME_USE_JEMALLOC_PROFILE
	if (third_party::internal::g_jemalloc_helper == nullptr)
	{
		OV_ASSERT2(false);
		return false;
	}

	third_party::internal::g_jemalloc_helper->Dump();
	return true;
#else	// OME_USE_JEMALLOC_PROFILE
	logw(third_party::internal::JEMALLOC_LOG_TAG, "Dumping jemalloc profile is not enabled. To enable it, define `OME_USE_JEMALLOC_PROFILE` in `global_config.mk`.");
	return false;
#endif	// OME_USE_JEMALLOC_PROFILE
}

const char *GetSpdlogVersion()
{
	static char version[32]{0};

	if (version[0] == '\0')
	{
		::snprintf(version, sizeof(version), "%d.%d.%d", SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
	}

	return version;
}

//--------------------------------------------------------------------
// Related to whisper.cpp
//--------------------------------------------------------------------

#include <whisper.h>

static void OnWhisperLog(enum ggml_log_level level, const char *text, void *ud)
{
	static const char *WHISPER_LOG_TAG = "Whisper";

	ov::String message(text);
	if (message.HasSuffix("\n"))
	{
		// Remove new line character
		message.SetLength(message.GetLength() - 1);
	}

	switch (level)
	{
		case GGML_LOG_LEVEL_ERROR:
			::ov_log_internal(OVLogLevelError, WHISPER_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;

		case GGML_LOG_LEVEL_WARN:
			::ov_log_internal(OVLogLevelWarning, WHISPER_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;

		case GGML_LOG_LEVEL_INFO:
			::ov_log_internal(OVLogLevelInformation, WHISPER_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;

		case GGML_LOG_LEVEL_NONE:
		case GGML_LOG_LEVEL_DEBUG:
		default:
			::ov_log_internal(OVLogLevelDebug, WHISPER_LOG_TAG, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", message.CStr());
			break;
	}
}

std::shared_ptr<ov::Error> InitializeWhisper()
{
	ggml_backend_load_all();

	whisper_log_set(OnWhisperLog, nullptr);

	return nullptr;
}

const char *GetWhisperCppVersion()
{
	return whisper_version();
}

const char *GetGgmlVersion()
{
	return ggml_version();
}
