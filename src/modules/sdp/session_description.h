//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"
#include "common_attr.h"
#include "media_description.h"

/*
 [Minimal SDP]

	"v=0\r\n"
	"o=OvenMediaEngine 1882243660 2 IN IP4 127.0.0.1\r\n"
	"s=-\r\n"
	"t=0 0\r\n"
	"c=IN IP4 0.0.0.0\r\n"
	"a=group:BUNDLE video\r\n"

	"a=fingerprint:sha-256 D7:81:CF:01:46:FB:2D:93:8E:04:AF:47:76:0A:88:08:FF:73:37:C6:A7:45:0B:31:FE:12:49:DE:A7:E4:1F:3A\r\n"
	"a=ice-options:trickle\r\n"
	"a=ice-pwd:c32d4070c67e9782bea90a9ab46ea838\r\n"
	"a=ice-ufrag:0dfa46c9\r\n"

	"a=msid-semantic:WMS *\r\n"

	"m=video 9 UDP/TLS/RTP/SAVPF 97\r\n"
	"a=rtpmap:97 VP8/50000\r\n"
	"a=framesize:97 426-240\r\n"

	"a=mid:video\r\n"
	"a=rtcp-mux\r\n"
	"a=setup:actpass\r\n"
	"a=sendonly\r\n"

	"a=ssrc:2064629418 cname:{b2266c86-259f-4853-8662-ea94cf0835a3}\r\n";
 */

class SessionDescription : public SdpBase,
                           public CommonAttr,
                           public ov::EnableSharedFromThis<SessionDescription>
{
public:
	enum class SdpType
	{
		Offer,
		Answer,
		Pranswer,
		Update,
		Rollback,
		Invalid
	};

	SessionDescription(const SdpType &type);
	~SessionDescription();

	SdpType GetType() const;

	void Release();

	bool FromString(const ov::String &sdp) override;

	// v=0
	void SetVersion(uint8_t version);
	uint8_t GetVersion() const;

	// o=OvenMediaEngine 1882243660 2 IN IP4 127.0.0.1
	void SetOrigin(ov::String user_name, uint32_t session_id, uint32_t session_version,
	               ov::String net_type, uint8_t ip_version, ov::String address);
	ov::String GetUserName() const;
	uint32_t GetSessionId() const;
	uint32_t GetSessionVersion() const;
	ov::String GetNetType() const;
	uint8_t GetIpVersion() const;
	ov::String GetAddress() const;

	// s=-
	void SetSessionName(ov::String session_name);
	ov::String GetSessionName() const;

	// t=0 0
	void SetTiming(uint32_t start, uint32_t stop);
	uint32_t GetStartTime() const;
	uint32_t GetStopTime() const;

	// a=msid-semantic:WMS *
	void SetMsidSemantic(const ov::String &semantic, const ov::String &token);
	ov::String GetMsidSemantic() const;
	ov::String GetMsidToken() const;

	void SetSdpLang(const ov::String &lang);
	ov::String GetSdpLang();

	void SetRange(const ov::String &range);
	ov::String GetRange();

	// m=video 9 UDP/TLS/RTP/SAVPF 97
	// a=group:BUNDLE 에 AddMedia의 mid를 추가한다. OME는 BUNDLE-ONLY만 지원한다. (2018.05.01)
	void AddMedia(const std::shared_ptr<const MediaDescription> &media);
	const std::shared_ptr<const MediaDescription> GetFirstMedia() const;
	const std::shared_ptr<const MediaDescription> GetMediaByMid(const ov::String &mid) const;
	const std::vector<std::shared_ptr<const MediaDescription>> &GetMediaList() const;

	// Common attr
	ov::String GetFingerprintAlgorithm() const override;
	ov::String GetFingerprintValue() const override;
	ov::String GetIceOption() const override;
	ov::String GetIceUfrag() const override;
	ov::String GetIcePwd() const override;

	bool operator ==(const SessionDescription &description) const
	{
		return (_session_id == description._session_id);
	}

private:
	bool UpdateData(ov::String &sdp) override;
	bool ParsingSessionLine(char type, std::string content);

	// SdpType
	SdpType _type = SdpType::Invalid;

	// version
	uint8_t _version = 0;

	// origin
	ov::String _user_name = "OvenMediaEngine";
	uint32_t _session_id = ov::Random::GenerateUInt32();
	uint32_t _session_version = 2;
	ov::String _net_type = "IN";
	uint8_t _ip_version = 4;
	ov::String _address = "127.0.0.1";

	// session
	ov::String _session_name = "-";

	// timing
	uint32_t _start_time = 0;
	uint32_t _stop_time = 0;

	// msid-semantic
	ov::String _msid_semantic;
	ov::String _msid_token;

	// a=sdplang
	ov::String _sdp_lang;

	// a=range:
	ov::String _range;

	// group:Bundle
	std::vector<ov::String> _bundles;

	// Media
	std::vector<std::shared_ptr<const MediaDescription>> _media_list;
};