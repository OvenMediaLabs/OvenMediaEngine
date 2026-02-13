//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "srt_option_processor.h"

#include <srt/version.h>

#include "srt_private.h"

std::shared_ptr<ov::Error> SrtOptionProcessor::SetOption(
	const std::shared_ptr<ov::Socket> &sock,
	const ov::String &name, const SRT_SOCKOPT option, const ov::String &value,
	const int32_t &dummy)
{
	logtt("Set an option for %s: %s = %s (int32_t)", sock->ToString().CStr(), name.CStr(), value.CStr());

	if (sock->SetSockOpt(option, ov::Converter::ToInt32(value)) == false)
	{
		return ov::Error::CreateError(OV_LOG_TAG, "Could not set an option: %s = %s (int32_t)", name.CStr(), value.CStr());
	}

	return nullptr;
}

std::shared_ptr<ov::Error> SrtOptionProcessor::SetOption(
	const std::shared_ptr<ov::Socket> &sock,
	const ov::String &name, const SRT_SOCKOPT option, const ov::String &value,
	const int64_t &dummy)
{
	logtt("Set an option for %s: %s = %s (int64_t)", sock->ToString().CStr(), name.CStr(), value.CStr());

	if (sock->SetSockOpt(option, ov::Converter::ToInt64(value)) == false)
	{
		return ov::Error::CreateError(OV_LOG_TAG, "Could not set an option: %s = %s (int64_t)", name.CStr(), value.CStr());
	}

	return nullptr;
}

std::shared_ptr<ov::Error> SrtOptionProcessor::SetOption(
	const std::shared_ptr<ov::Socket> &sock,
	const ov::String &name, const SRT_SOCKOPT option, const ov::String &value,
	const bool &dummy)
{
	logtt("Set an option for %s: %s = %s (bool)", sock->ToString().CStr(), name.CStr(), value.CStr());

	if (sock->SetSockOpt(option, ov::Converter::ToBool(value)) == false)
	{
		return ov::Error::CreateError(OV_LOG_TAG, "Could not set an option: %s = %s (bool)", name.CStr(), value.CStr());
	}

	return nullptr;
}

std::shared_ptr<ov::Error> SrtOptionProcessor::SetOption(
	const std::shared_ptr<ov::Socket> &sock,
	const ov::String &name, const SRT_SOCKOPT option, const ov::String &value,
	const linger &dummy)
{
	linger lin;

	lin.l_linger = ov::Converter::ToInt32(value);
	lin.l_onoff = (lin.l_linger > 0) ? 1 : 0;

	logtt("Set an option for %s: %s = { .l_linger = %d, .l_onoff = %d } (linger)", sock->ToString().CStr(), name.CStr(), value.CStr(), lin.l_linger, lin.l_onoff);

	return ov::Error::CreateError(OV_LOG_TAG, "Could not set an option: %s = { .l_linger = %d, .l_onoff = %d } (linger)", name.CStr(), lin.l_linger, lin.l_onoff);

	if (sock->SetSockOpt(option, lin) == false)
	{
		return ov::Error::CreateError(OV_LOG_TAG, "Could not set an option: %s = { .l_linger = %d, .l_onoff = %d } (linger)", name.CStr(), lin.l_linger, lin.l_onoff);
	}

	return nullptr;
}

std::shared_ptr<ov::Error> SrtOptionProcessor::SetOption(
	const std::shared_ptr<ov::Socket> &sock,
	const ov::String &name, const SRT_SOCKOPT option, const ov::String &value,
	const ov::String &dummy)
{
	logtt("Set an option for %s: %s = \"%s\" (ov::String)", sock->ToString().CStr(), name.CStr(), value.CStr());

	if (sock->SetSockOpt(option, value.CStr(), value.GetLength()) == false)
	{
		return ov::Error::CreateError(OV_LOG_TAG, "Could not set an option: %s = \"%s\" (string)", name.CStr(), value.CStr());
	}

	return nullptr;
}

#define SRT_SET_OPTION(srt_option, type)                                       \
	if (name == #srt_option)                                                   \
	{                                                                          \
		const auto __error = SetOption(sock, name, srt_option, value, type{}); \
                                                                               \
		if (__error != nullptr)                                                \
		{                                                                      \
			return __error;                                                    \
		}                                                                      \
                                                                               \
		continue;                                                              \
	}

#define SRT_NOT_SUPPORTED_OPTION(srt_option, type, message, ...)                                                      \
	if (name == #srt_option)                                                                                          \
	{                                                                                                                 \
		return ov::Error::CreateError(OV_LOG_TAG, "Cannot use an option: %s - " message, name.CStr(), ##__VA_ARGS__); \
	}

#define IS_SRT_VERSION_EQ_OR_GT(major, minor, patch) \
	SRT_MAKE_VERSION(major, minor, patch) <= SRT_VERSION_VALUE

std::shared_ptr<ov::Error> SrtOptionProcessor::SetOptions(
	const std::shared_ptr<ov::Socket> &sock,
	const cfg::cmn::Options &options)
{
	for (auto &option : options.GetOptionList())
	{
		const auto name = option.GetKey();
		const auto value = option.GetValue();

		SRT_SET_OPTION(SRTO_BINDTODEVICE, ov::String);
		SRT_SET_OPTION(SRTO_CONGESTION, ov::String);
		SRT_SET_OPTION(SRTO_CONNTIMEO, int32_t);
#ifndef ENABLE_AEAD_API_PREVIEW
		// SRTO_CRYPTOMODE is only available in SRT 1.5.2 or later with ENABLE_AEAD_API_PREVIEW=true
		SRT_NOT_SUPPORTED_OPTION(SRTO_CRYPTOMODE, int32_t, "Not supported in " SRT_VERSION_STRING " (This option is available starting from version 1.5.2)");
#else	// ENABLE_AEAD_API_PREVIEW
		SRT_SET_OPTION(SRTO_CRYPTOMODE, int32_t);
#endif	// ENABLE_AEAD_API_PREVIEW
		SRT_SET_OPTION(SRTO_DRIFTTRACER, bool);
		SRT_SET_OPTION(SRTO_ENFORCEDENCRYPTION, bool);
		SRT_SET_OPTION(SRTO_EVENT, int32_t);
		SRT_SET_OPTION(SRTO_FC, int32_t);
#if IS_SRT_VERSION_EQ_OR_GT(1, 5, 1)
		// SRT version is 1.5.1 or later
		SRT_SET_OPTION(SRTO_GROUPCONNECT, int32_t);
		SRT_SET_OPTION(SRTO_GROUPMINSTABLETIMEO, int32_t);
		SRT_SET_OPTION(SRTO_GROUPTYPE, int32_t);
#else	// IS_SRT_VERSION_EQ_OR_GT(1, 5, 1)
		SRT_NOT_SUPPORTED_OPTION(SRTO_GROUPCONNECT, int32_t, "Not supported in " SRT_VERSION_STRING " (This option is available starting from version 1.5.1)");
		SRT_NOT_SUPPORTED_OPTION(SRTO_GROUPMINSTABLETIMEO, int32_t, "Not supported in " SRT_VERSION_STRING " (This option is available starting from version 1.5.1)");
		SRT_NOT_SUPPORTED_OPTION(SRTO_GROUPTYPE, int32_t, "Not supported in " SRT_VERSION_STRING " (This option is available starting from version 1.5.1)");
#endif	// IS_SRT_VERSION_EQ_OR_GT(1, 5, 1)
		SRT_SET_OPTION(SRTO_INPUTBW, int64_t);
		SRT_SET_OPTION(SRTO_IPTOS, int32_t);
		SRT_SET_OPTION(SRTO_IPTTL, int32_t);
		SRT_SET_OPTION(SRTO_IPV6ONLY, int32_t);
		SRT_SET_OPTION(SRTO_ISN, int32_t);
		SRT_SET_OPTION(SRTO_KMPREANNOUNCE, int32_t);
		SRT_SET_OPTION(SRTO_KMREFRESHRATE, int32_t);
		SRT_SET_OPTION(SRTO_KMSTATE, int32_t);
		SRT_SET_OPTION(SRTO_LATENCY, int32_t);
		SRT_SET_OPTION(SRTO_LINGER, linger);
		SRT_SET_OPTION(SRTO_LOSSMAXTTL, int32_t);
		SRT_SET_OPTION(SRTO_MAXBW, int64_t);
		SRT_SET_OPTION(SRTO_MESSAGEAPI, bool);
		SRT_SET_OPTION(SRTO_MININPUTBW, int64_t);
		SRT_SET_OPTION(SRTO_MINVERSION, int32_t);
		SRT_SET_OPTION(SRTO_MSS, int32_t);
		SRT_SET_OPTION(SRTO_NAKREPORT, bool);
		SRT_SET_OPTION(SRTO_OHEADBW, int32_t);
		SRT_SET_OPTION(SRTO_PACKETFILTER, ov::String);
		SRT_SET_OPTION(SRTO_PASSPHRASE, ov::String);
		SRT_SET_OPTION(SRTO_PAYLOADSIZE, int32_t);
		SRT_SET_OPTION(SRTO_PBKEYLEN, int32_t);
		SRT_SET_OPTION(SRTO_PEERIDLETIMEO, int32_t);
		SRT_SET_OPTION(SRTO_PEERLATENCY, int32_t);
		SRT_SET_OPTION(SRTO_PEERVERSION, int32_t);
		SRT_SET_OPTION(SRTO_RCVBUF, int32_t);
		SRT_SET_OPTION(SRTO_RCVDATA, int32_t);
		SRT_SET_OPTION(SRTO_RCVKMSTATE, int32_t);
		SRT_SET_OPTION(SRTO_RCVLATENCY, int32_t);
		SRT_NOT_SUPPORTED_OPTION(SRTO_RCVSYN, bool, "OME will decide whether to use sync or async mode");
		SRT_SET_OPTION(SRTO_RCVTIMEO, int32_t);
		SRT_SET_OPTION(SRTO_RENDEZVOUS, bool);
		SRT_SET_OPTION(SRTO_RETRANSMITALGO, int32_t);
		SRT_SET_OPTION(SRTO_REUSEADDR, bool);
		SRT_SET_OPTION(SRTO_SENDER, bool);
		SRT_SET_OPTION(SRTO_SNDBUF, int32_t);
		SRT_SET_OPTION(SRTO_SNDDATA, int32_t);
		SRT_SET_OPTION(SRTO_SNDDROPDELAY, int32_t);
		SRT_SET_OPTION(SRTO_SNDKMSTATE, int32_t);
		SRT_NOT_SUPPORTED_OPTION(SRTO_SNDSYN, bool, "OME will decide whether to use sync or async mode");
		SRT_SET_OPTION(SRTO_SNDTIMEO, int32_t);
		SRT_SET_OPTION(SRTO_STATE, int32_t);
		SRT_SET_OPTION(SRTO_STREAMID, ov::String);
		SRT_SET_OPTION(SRTO_TLPKTDROP, bool);
		SRT_SET_OPTION(SRTO_TRANSTYPE, int32_t);
		SRT_SET_OPTION(SRTO_TSBPDMODE, bool);
		SRT_SET_OPTION(SRTO_UDP_RCVBUF, int32_t);
		SRT_SET_OPTION(SRTO_UDP_SNDBUF, int32_t);
		SRT_SET_OPTION(SRTO_VERSION, int32_t);

		return ov::Error::CreateError(OV_LOG_TAG, "No such option: %s", name.CStr());
	}

	return nullptr;
}