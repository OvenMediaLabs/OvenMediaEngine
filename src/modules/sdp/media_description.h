//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//
//==============================================================================

#pragma once

#include "sdp_base.h"
#include "common_attr.h"
#include "payload_attr.h"
#include "simulcast_attr.h"

class SessionDescription;

class MediaDescription : public SdpBase, public CommonAttr
{
public:

	enum class MediaType
	{
		Unknown,
		Video,
		Audio,
		Application
	};

	enum class Direction
	{
		Unknown,
		SendRecv,
		RecvOnly,
		SendOnly,
		Inactive
	};

	explicit MediaDescription();
	virtual ~MediaDescription();

	bool FromString(const ov::String &desc) override;

	// m=video 9 UDP/TLS/RTP/SAVPF 97
	void SetMediaType(MediaType type);
	bool SetMediaType(const ov::String &type);
	const MediaType GetMediaType() const;
	const ov::String GetMediaTypeStr() const;
	void SetPort(uint16_t port);
	uint16_t GetPort() const;
	void UseDtls(bool flag);
	bool IsUseDtls() const;

	void AddPayload(const std::shared_ptr<PayloadAttr> &payload);
	std::shared_ptr<const PayloadAttr> GetPayload(uint8_t id) const;
	std::shared_ptr<PayloadAttr> GetPayload(uint8_t id);
	std::shared_ptr<const PayloadAttr> GetFirstPayload() const;
	// payload list
	const std::vector<std::shared_ptr<PayloadAttr>> &GetPayloadList() const;

	// Add rid, whether to be used in simulcast
	// alternative_to_base_rid : This is set as an alternative to a specific rid in simulcast. If it is empty, it is to be used for new base rid.
	// That is, it is set as a replacement for the base rid of the existing stream.
	// For example, when there is a=simulcast:send 1;2 and AddRid(3, true, 2) is called, it is configured as follows.
	// a=simulcast:send 1;2,3
	bool AddRid(const std::shared_ptr<RidAttr> &rid, bool simulcast = true, const ov::String &alternative_to_base_rid = "");
	// From SDP line
	// rid_id : rid id
	// direction : send, recv
	// restrictions : pt=97,98;max-width=1280;max-height=720 ...
	bool AddRid(const ov::String &rid_id, const ov::String &direction, const ov::String &restrictions);
	// From SDP line
	bool SetSimulcast(const ov::String &send_rids, const ov::String &recv_rids);
	bool AddSendLayerToSimulcast(const std::shared_ptr<SimulcastLayer> &layer);
	bool AddRecvLayerToSimulcast(const std::shared_ptr<SimulcastLayer> &layer);

	std::shared_ptr<RidAttr> GetRid(const ov::String &id);
	// rid list
	std::vector<std::shared_ptr<RidAttr>> GetRidList() const;
	// simulcast list
	std::shared_ptr<SimulcastLayer> GetSimulcastLayer(const ov::String &base_rid);
	std::vector<std::shared_ptr<SimulcastLayer>> GetSendLayerList() const;
	std::vector<std::shared_ptr<SimulcastLayer>> GetRecvLayerList() const;

	// a=rtcp-mux
	void UseRtcpMux(bool flag = true);
	bool IsRtcpMux() const;

	// a=rtcp-rsize
	void UseRtcpRsize(bool flag = true);
	bool IsUseRtcpRsize() const;

	// a=sendonly
	void SetDirection(Direction dir);
	bool SetDirection(const ov::String &dir);
	const Direction GetDirection() const;

	// a=mid:video
	void SetMid(const ov::String &mid);
	std::optional<ov::String> GetMid() const;

	// a=msid:msid appdata
	void SetMsid(const ov::String &msid, const ov::String &msid_appdata);
	std::optional<ov::String> GetMsid();
	std::optional<ov::String> GetMsidAppdata();

	// c=IN IP4 0.0.0.0
	void SetConnection(uint8_t version, const ov::String &ip);

	// a=framerate:29.97
	void SetFramerate(float framerate);
	const float GetFramerate() const;

	// a=rtpmap:96 VP8/50000
	bool AddRtpmap(uint8_t payload_type, const ov::String &codec, uint32_t rate,
	               const ov::String &parameters);

	// a=rtcp-fb:96 nack pli
	bool EnableRtcpFb(uint8_t id, const ov::String &type, bool on);
	void EnableRtcpFb(uint8_t id, const PayloadAttr::RtcpFbType &type, bool on);

	// a=fmtp:96 packetization-mode=1;profile-level-id=42e01f;level-asymmetry-allowed=1
	// a=fmtp:96 profile-level-id=1;moe=AAC-hbr;sizelength=13
	void SetFmtp(uint8_t id, const ov::String &fmtp);

	// a=ssrc:2064629418 cname:{b2266c86-259f-4853-8662-ea94cf0835a3}
	void SetCname(const ov::String &cname);
	void SetSsrc(uint32_t ssrc);
	void SetRtxSsrc(uint32_t rtx_ssrc);

	std::optional<uint32_t> GetSsrc() const;
	std::optional<uint32_t> GetRtxSsrc() const;
	std::optional<ov::String> GetCname() const;

	// a=extmap:1 urn:ietf:params:rtp-hdrext:framemarking
	void AddExtmap(uint8_t id, ov::String attribute);
	std::map<uint8_t, ov::String> GetExtmap() const;
	ov::String GetExtmapItem(uint8_t id) const;
	bool FindExtmapItem(const ov::String &keyword, uint8_t &id, ov::String &uri) const;

private:
	bool UpdateData(ov::String &sdp) override;
	bool ParsingMediaLine(char type, std::string content);

	MediaType _media_type = MediaType::Unknown;
	ov::String _media_type_str = "UNKNOWN";

	uint16_t _port = 9;
	bool _use_dtls_flag = false;
	ov::String _protocol;

	bool _use_rtcpmux_flag = false;
	bool _use_rtcprsize_flag = false;

	Direction _direction = Direction::Unknown;
	ov::String _direction_str = "UNKNOWN";

	std::optional<ov::String> _msid;
	std::optional<ov::String> _msid_appdata;

	uint8_t _connection_ip_version = 4;
	ov::String _connection_ip = "0.0.0.0";

	float _framerate = 0.0f;

	std::optional<ov::String> _mid;
	std::optional<ov::String> _cname;
	std::optional<uint32_t> _ssrc;
	std::optional<uint32_t> _rtx_ssrc;
	
	std::map<uint8_t, ov::String> _extmap;

	std::vector<std::shared_ptr<PayloadAttr>> _payload_list;
	std::vector<std::shared_ptr<RidAttr>> _rid_list;
	std::vector<std::shared_ptr<SimulcastLayer>> _send_layers;
	std::vector<std::shared_ptr<SimulcastLayer>> _recv_layers;

	std::queue<std::function<void()>> _post_parsing_tasks;
};
