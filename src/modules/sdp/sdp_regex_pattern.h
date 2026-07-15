//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//	
//	Getroot
//==============================================================================
#pragma once

#include "base/common_types.h"
#include "common_attr.h"

#define RegisterPattern(member, pattern) 											\
{																					\
	member = ov::Regex(pattern); 													\
	auto error = member.Compile();													\
	if(error != nullptr)															\
	{																				\
		loge("SDPRegex", "SDP regex pattern compile error : %s", error->What());	\
		return false;																\
	}																				\
}			

#define RegisterMatchFunction(var, func) \
	ov::MatchResult func(const char *subject)			\
	{													\
		if(_built == false)								\
		{												\
			return {};									\
		}												\
		return var.Matches(subject);					\
	}													\

class SDPRegexPattern : public ov::Singleton<SDPRegexPattern>
{
public:
	bool Compile()
	{
		RegisterPattern(_version_pattern, R"(^(\d*)$)");
		RegisterPattern(_origin_pattern, R"(^(\S*) (\d*) (\d*) (\S*) IP(\d) (\S*))");
		RegisterPattern(_session_name_pattern, R"(^(.*))");
		RegisterPattern(_timing_pattern, R"(^(\d*) (\d*))");
		RegisterPattern(_group_bundle_pattern, R"(^group:BUNDLE (.*))");
		
		RegisterPattern(_msid_semantic_pattern, R"(^msid-semantic:\s?(\w*) (\S*))");
		RegisterPattern(_sdplang_pattern, R"(^sdplang:(\S*))");
		RegisterPattern(_range_pattern, R"(^range:(\S*))");

		RegisterPattern(_fingerprint_pattern, R"(^fingerprint:(\S*) (\S*))");
		RegisterPattern(_ice_options_pattern, R"(^ice-options:(\S*))");
		RegisterPattern(_ice_ufrag_pattern, R"(^ice-ufrag:(\S*))");
		RegisterPattern(_ice_pwd_pattern, R"(^ice-pwd:(\S*))");
		RegisterPattern(_control_pattern, R"(^control:(\S*))");

		RegisterPattern(_media_pattern, R"(^(\w*) (\d*) ([\w\/]*)(?: (.*))?)");
		RegisterPattern(_connection_pattern, R"(^IN IP(\d) (\S*))");
		RegisterPattern(_rtp_map_pattern, R"(rtpmap:(\d*) ([\w\-\.]*)(?:\s*\/(\d*)(?:\s*\/(\S*))?)?)");
											
		RegisterPattern(_rtcp_mux_pattern, R"(^(rtcp-mux))");
		RegisterPattern(_rtcp_rsize_pattern, R"(^(rtcp-rsize))");
		RegisterPattern(_rtcp_fb_pattern, R"(rtcp-fb:(\*|\d*) (.*))");

		RegisterPattern(_mid_pattern, R"(^mid:([^\s]*))");
		RegisterPattern(_msid_pattern, R"(^msid:(.*) (.*))");

		RegisterPattern(_setup_pattern, R"(^setup:(\w*))");

		RegisterPattern(_ssrc_cname_pattern, R"(^ssrc:(\d*) cname(?::(.*))?)");
		RegisterPattern(_ssrc_fid_pattern, R"(^ssrc-group:FID ([0-9]*) ([0-9]*))");

		RegisterPattern(_framerate_pattern, R"(^framerate:(\d+(?:$|\.\d+)))");
		RegisterPattern(_direction_pattern, R"(^(sendrecv|recvonly|sendonly|inactive))");

		RegisterPattern(_fmtp_pattern, R"(^fmtp:(\*|\d*) (.*))");
		RegisterPattern(_extmap_pattern, R"(^extmap:([\w_/]*) (\S*)(?: (\S*))?)");

		RegisterPattern(_rid_pattern, R"(^rid:([a-zA-Z0-9\-_]+)\s+(send|recv)(?:\s+(.*))?)");
		RegisterPattern(_simulcast_pattern, R"(^simulcast:\s*(send|recv)\s+([a-zA-Z0-9\-_\,;]+)(?:\s*(send|recv)\s+([a-zA-Z0-9\-_\,;]+))?)");
		_built = true;

		return true;
	}

	RegisterMatchFunction(_version_pattern, MatchVersion)
	RegisterMatchFunction(_origin_pattern, MatchOrigin)
	RegisterMatchFunction(_session_name_pattern, MatchSessionName)
	RegisterMatchFunction(_timing_pattern, MatchTiming)
	RegisterMatchFunction(_group_bundle_pattern, MatchGourpBundle)
	RegisterMatchFunction(_msid_semantic_pattern, MatchMsidSemantic)
	RegisterMatchFunction(_sdplang_pattern, MatchSdpLang)
	RegisterMatchFunction(_range_pattern, MatchRange)

	RegisterMatchFunction(_fingerprint_pattern, MatchFingerprint)
	RegisterMatchFunction(_ice_options_pattern, MatchIceOption)
	RegisterMatchFunction(_ice_ufrag_pattern, MatchIceUfrag)
	RegisterMatchFunction(_ice_pwd_pattern, MatchIcePwd)
	RegisterMatchFunction(_control_pattern, MatchControl)

	RegisterMatchFunction(_media_pattern, MatchMedia)
	RegisterMatchFunction(_connection_pattern, MatchConnection)
	RegisterMatchFunction(_rtp_map_pattern, MatchRtpmap)
	RegisterMatchFunction(_rtcp_mux_pattern, MatchRtcpMux)
	RegisterMatchFunction(_rtcp_rsize_pattern, MatchRtcpRsize)
	RegisterMatchFunction(_rtcp_fb_pattern, MatchRtcpFb)

	RegisterMatchFunction(_mid_pattern, MatchMid)
	RegisterMatchFunction(_msid_pattern, MatchMsid)

	RegisterMatchFunction(_setup_pattern, MatchSetup)
	RegisterMatchFunction(_ssrc_cname_pattern, MatchSsrcCname)
	RegisterMatchFunction(_ssrc_fid_pattern, MatchSsrcFid)

	RegisterMatchFunction(_framerate_pattern, MatchFramerate)
	RegisterMatchFunction(_direction_pattern, MatchDirection)

	RegisterMatchFunction(_fmtp_pattern, MatchFmtp)
	RegisterMatchFunction(_extmap_pattern, MatchExtmap)

	RegisterMatchFunction(_rid_pattern, MatchRid)
	RegisterMatchFunction(_simulcast_pattern, MatchSimulcast)

private:
	bool _built = false;

	ov::Regex _version_pattern;	// v=
	ov::Regex _origin_pattern; // o=
	ov::Regex _session_name_pattern; // s=
	ov::Regex _timing_pattern; // t=
	ov::Regex _group_bundle_pattern; // a=group:BUNDLE
	ov::Regex _msid_semantic_pattern; // a=msid-semantic:
	ov::Regex _sdplang_pattern; // a=sdplang:
	ov::Regex _range_pattern; // a=range:

	ov::Regex _fingerprint_pattern; // a=fingerprint:
	ov::Regex _ice_options_pattern; // a=ice-options:
	ov::Regex _ice_ufrag_pattern; // a=ice-ufrag
	ov::Regex _ice_pwd_pattern; // a=ice-pwd
	ov::Regex _control_pattern; // a=control

	ov::Regex _media_pattern; // m=video 9 UDP/TLS/RTP/SAVPF 97 98 99 100
	ov::Regex _connection_pattern; // c=IN IP4 0.0.0.0
	ov::Regex _rtp_map_pattern; // a=rtpmap:
	ov::Regex _rtcp_mux_pattern; // a=rtcp-mux
	ov::Regex _rtcp_rsize_pattern; // a=rtcp-rsize
	ov::Regex _rtcp_fb_pattern; // a=rtcp-fb:

	ov::Regex _mid_pattern; // a=mid:
	ov::Regex _msid_pattern; // a=msid: audio video
	ov::Regex _setup_pattern; // a=setup:

	ov::Regex _ssrc_cname_pattern; // a=ssrc:111 cname:10101
	ov::Regex _ssrc_fid_pattern; // a=ssrc-group:FID 100 101

	ov::Regex _framerate_pattern; // a=framerate:
	ov::Regex _direction_pattern; // a=sendrecv|recvonly|sendonly|inactive

	ov::Regex _fmtp_pattern; // a=fmtp:96 packetization-mode=xx ~~
	ov::Regex _extmap_pattern; // a=extmap:1 urn:ietf:params:~

	ov::Regex _rid_pattern; // a=rid:1 send pt=97,98;max-width=1280;max-height=720
	ov::Regex _simulcast_pattern; // a=simulcast:send 1;2,3
};