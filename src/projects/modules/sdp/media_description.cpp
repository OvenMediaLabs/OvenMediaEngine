//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "media_description.h"

#include <regex>

#include "sdp_regex_pattern.h"
#include "session_description.h"

#include "sdp_private.h"

MediaDescription::MediaDescription()
{
	UseDtls(true);
	UseRtcpMux(false);
}

MediaDescription::~MediaDescription()
{
}

bool MediaDescription::UpdateData(ov::String &sdp)
{
	// Make m line
	sdp = ov::String::FormatString("m=%s %d %s", _media_type_str.CStr(), _port, _protocol.CStr());

	// Append Payload id
	for (auto &payload : _payload_list)
	{
		if (payload->GetId() == 0)
		{
			return false;
		}

		sdp.AppendFormat(" %d", payload->GetId());
	}

	sdp.AppendFormat(
		"\r\n"
		"c=IN IP%d %s\r\n"
		"a=%s\r\n"
		"a=mid:%s\r\n"
		"a=setup:%s\r\n",
		_connection_ip_version, _connection_ip.CStr(),
		_direction_str.CStr(),
		_mid.value_or("").CStr(),
		GetSetupStr().CStr());

	// Common Attributes
	ov::String common_attr_text;
	if (SerializeCommonAttr(common_attr_text) == false)
	{
		return false;
	}

	sdp.Append(common_attr_text);

	if (_framerate)
	{
		sdp.AppendFormat("a=framerate:%.2f\r\n", _framerate);
	}

	if (_use_rtcpmux_flag)
	{
		sdp.AppendFormat("a=rtcp-mux\r\n");
	}

	if (_use_rtcprsize_flag)
	{
		sdp.AppendFormat("a=rtcp-rsize\r\n");
	}

	if (_msid_appdata.has_value() == true)
	{
		sdp.AppendFormat("a=msid:%s %s\r\n", _msid.value_or("").CStr(), _msid_appdata.value_or("").CStr());
	}

	// Extmap
	for (const auto &[id, attribute] : _extmap)
	{
		sdp.AppendFormat("a=extmap:%d %s\r\n", id, attribute.CStr());
	}

	// Payloads
	for (auto &payload : _payload_list)
	{
		uint8_t payload_id = payload->GetId();

		sdp.AppendFormat("a=rtpmap:%d %s/%d", payload_id,
						 payload->GetCodecStr().CStr(),
						 payload->GetCodecRate());

		if (payload->GetCodecParams().IsEmpty() == false)
		{
			sdp.AppendFormat("/%s", payload->GetCodecParams().CStr());
		}

		sdp.Append("\r\n");

		// a=fmtp:%d packetization-mode=1
		auto fmtp = payload->GetFmtp();

		if (fmtp.IsEmpty() == false)
		{
			sdp.AppendFormat("a=fmtp:%d ", payload_id);
			sdp.Append(fmtp);
			sdp.Append("\r\n");
		}

		if (payload->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::GoogRemb))
		{
			sdp.AppendFormat("a=rtcp-fb:%d goog-remb\r\n", payload_id);
		}
		if (payload->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::TransportCc))
		{
			sdp.AppendFormat("a=rtcp-fb:%d transport-cc\r\n", payload_id);
		}
		if (payload->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::CcmFir))
		{
			sdp.AppendFormat("a=rtcp-fb:%d ccm fir\r\n", payload_id);
		}
		if (payload->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::Nack))
		{
			sdp.AppendFormat("a=rtcp-fb:%d nack\r\n", payload_id);
		}
		if (payload->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::NackPli))
		{
			sdp.AppendFormat("a=rtcp-fb:%d nack pli\r\n", payload_id);
		}
	}

	// rid 
	for (auto &rid : _rid_list)
	{
		sdp.AppendFormat("%s\r\n", rid->ToString().CStr());
	}

	// Simulcast
	// a=simulcast:send 1,2;3,4 recv 5;6
	if (_send_layers.empty() == false || _recv_layers.empty() == false)
	{
		sdp.Append("a=simulcast:");

		// send layer
		if (_send_layers.empty() == false)
		{
			sdp.Append("send ");
		}
		for (auto &layer : _send_layers)
		{
			for (auto &rid : layer->GetRidList())
			{
				sdp.AppendFormat("%s", rid->GetId().CStr());
				// if not last rid
				if (rid != layer->GetRidList().back())
				{
					sdp.Append(",");
				}
			}
			// if not last layer
			if (layer != _send_layers.back())
			{
				sdp.Append(";");
			}
		}
		// recv layer
		if (_recv_layers.empty() == false)
		{
			sdp.Append(" recv ");
		}
		for (auto &layer : _recv_layers)
		{
			for (auto &rid : layer->GetRidList())
			{
				sdp.AppendFormat("%s", rid->GetId().CStr());
				// if not last rid
				if (rid != layer->GetRidList().back())
				{
					sdp.Append(",");
				}
			}
			// if not last layer
			if (layer != _recv_layers.back())
			{
				sdp.Append(";");
			}
		}

		sdp.Append("\r\n");
	}

	// SSRCs
	if (_cname.has_value())
	{
		if (_rtx_ssrc.has_value())
		{
			sdp.AppendFormat("a=ssrc-group:FID %u %u\r\n", _ssrc, _rtx_ssrc);
		}
		sdp.AppendFormat("a=ssrc:%u cname:%s\r\n", _ssrc, _cname.value_or("").CStr());

		if (_msid_appdata.has_value())
		{
			sdp.AppendFormat("a=ssrc:%u msid:%s %s\r\n", _ssrc, _msid.value_or("").CStr(), _msid_appdata.value_or("").CStr());
			sdp.AppendFormat("a=ssrc:%u mslabel:%s\r\n", _ssrc, _msid.value_or("").CStr());
			sdp.AppendFormat("a=ssrc:%u label:%s\r\n", _ssrc, _msid_appdata.value_or("").CStr());
		}

		if (_rtx_ssrc.has_value())
		{
			sdp.AppendFormat("a=ssrc:%u cname:%s\r\n", _rtx_ssrc, _cname.value_or("").CStr());
			if (_msid_appdata.has_value())
			{
				sdp.AppendFormat("a=ssrc:%u msid:%s %s\r\n", _rtx_ssrc, _msid.value_or("").CStr(), _msid_appdata.value_or("").CStr());
				sdp.AppendFormat("a=ssrc:%u mslabel:%s\r\n", _rtx_ssrc, _msid.value_or("").CStr());
				sdp.AppendFormat("a=ssrc:%u label:%s\r\n", _rtx_ssrc, _msid_appdata.value_or("").CStr());
			}
		}
	}

	return true;
}

bool MediaDescription::SetMediaType(const ov::String &type)
{
	if (type.UpperCaseString() == "VIDEO")
	{
		SetMediaType(MediaType::Video);
	}
	else if (type.UpperCaseString() == "AUDIO")
	{
		SetMediaType(MediaType::Audio);
	}
	else if (type.UpperCaseString() == "APPLICATION")
	{
		SetMediaType(MediaType::Application);
	}
	else
	{
		return false;
	}

	return true;
}

bool MediaDescription::FromString(const ov::String &desc)
{
	std::stringstream sdpstream(desc.CStr());
	std::string line;

	while (std::getline(sdpstream, line, '\n'))
	{
		if (line.size() && line[line.length() - 1] == '\r')
		{
			line.pop_back();
		}

		char type = line[0];
		std::string content = line.substr(2);

		if (ParsingMediaLine(type, content) == false)
		{
			logw("SDP", "Could not parse line: %c: %s (%s)", type, content.c_str(), line.c_str());
			return false;
		}
	}

	// Run post parsing tasks
	while (_post_parsing_tasks.empty() == false)
	{
		_post_parsing_tasks.front()();
		_post_parsing_tasks.pop();
	}

	Update();

	return true;
}

bool MediaDescription::SetDirection(const ov::String &dir)
{
	if (dir.UpperCaseString() == "SENDRECV")
	{
		SetDirection(Direction::SendRecv);
	}
	else if (dir.UpperCaseString() == "RECVONLY")
	{
		SetDirection(Direction::RecvOnly);
	}
	else if (dir.UpperCaseString() == "SENDONLY")
	{
		SetDirection(Direction::SendOnly);
	}
	else if (dir.UpperCaseString() == "INACTIVE")
	{
		SetDirection(Direction::Inactive);
	}
	else
	{
		return false;
	}

	return true;
}

bool MediaDescription::ParsingMediaLine(char type, std::string content)
{
	bool parsing_error = false;
	std::smatch matches;

	switch (type)
	{
		case 'm':
			// m=video 9 UDP/TLS/RTP/SAVPF 97
			{
				auto match = SDPRegexPattern::GetInstance()->MatchMedia(content.c_str());
				if (match.GetGroupCount() != 4 + 1)
				{
					parsing_error = true;
					break;
				}

				if (!SetMediaType(match.GetGroupAt(1).GetValue()))
				{
					parsing_error = true;
					break;
				}

				SetPort(ov::Converter::ToUInt32(match.GetGroupAt(2).GetValue().CStr()));

				ov::String protocol = match.GetGroupAt(3).GetValue();
				if (protocol.UpperCaseString() == "UDP/TLS/RTP/SAVPF" || protocol.UpperCaseString() == "UDP/TLS/RTP/SAVP")
				{
					UseDtls(true);
				}
				else if (protocol.UpperCaseString() == "RTP/AVPF" || protocol.UpperCaseString() == "RTP/AVP")
				{
					UseDtls(false);
				}
				else
				{
					loge("SDP", "Cannot support %s protocol", protocol.CStr());
					parsing_error = true;
					break;
				}

				auto payload_numbers = match.GetGroupAt(4).GetValue();
				auto items = payload_numbers.Split(" ");
				for (const auto &item : items)
				{
					auto payload = std::make_shared<PayloadAttr>();
					payload->SetId(ov::Converter::ToUInt32(item.CStr()));
					AddPayload(payload);
				}
			}
			break;
		case 'c':
			// c=IN IP4 0.0.0.0
			{
				auto match = SDPRegexPattern::GetInstance()->MatchConnection(content.c_str());
				if (match.GetGroupCount() != 2 + 1)
				{
					break;
				}

				SetConnection(
					ov::Converter::ToUInt32(match.GetGroupAt(1).GetValue().CStr()),
					match.GetGroupAt(2).GetValue());
			}
			break;

		case 'a':
			// For Performance
			if (content.compare(0, OV_COUNTOF("rtp") - 1, "rtp") == 0)
			{
				// a=rtpmap:96 VP8/50000/?
				auto match = SDPRegexPattern::GetInstance()->MatchRtpmap(content.c_str());
				if (match.GetGroupCount() < 3 + 1)
				{
					parsing_error = true;
					break;
				}

				AddRtpmap(
					ov::Converter::ToUInt32(match.GetGroupAt(1).GetValue().CStr()),
					match.GetGroupAt(2).GetValue(),
					ov::Converter::ToUInt32(match.GetGroupAt(3).GetValue().CStr()),
					match.GetGroupAt(4).GetValue());
			}
			else if (content.compare(0, OV_COUNTOF("rid") - 1, "rid") == 0)
			{
				// a=rid:1 send pt=100;max-width=1280;max-height=720;max-fps=60;depend=2
				// a=rid:2 send pt=101;max-width=1280;max-height=720;max-fps=30
				// a=rid:3 send pt=101;max-width=640;max-height=360
				// a=rid:4 send pt=103;max-width=640;max-height=360
				auto match = SDPRegexPattern::GetInstance()->MatchRid(content.c_str());
				if (match.GetGroupCount() < 1 + 2)
				{
					parsing_error = true;
					break;
				}
				
				if (AddRid(match.GetGroupAt(1).GetValue(), match.GetGroupAt(2).GetValue(), match.GetGroupAt(3).GetValue()) == false)
				{
					parsing_error = true;
					break;
				}
			}
			// a=simulcast
			else if (content.compare(0, OV_COUNTOF("simulcast") - 1, "simulcast") == 0)
			{
				// a=simulcast:send 1;2,3 recv 4
				auto match = SDPRegexPattern::GetInstance()->MatchSimulcast(content.c_str());

				// 1 + 4 groups
				if (match.GetGroupCount() < 1 + 2)
				{
					parsing_error = true;
					break;
				}

				_post_parsing_tasks.push([=]() {
					// It must be run after parsing all rid lines
					SetSimulcast(match.GetGroupAt(2).GetValue(), match.GetGroupAt(4).GetValue());
				});

			}
			else if (content.compare(0, OV_COUNTOF("rtcp-m") - 1, "rtcp-m") == 0)
			{
				// a=rtcp-mux
				auto match = SDPRegexPattern::GetInstance()->MatchRtcpMux(content.c_str());
				if (match.GetError() == nullptr)
				{
					UseRtcpMux(true);
				}
			}
			else if (content.compare(0, OV_COUNTOF("rtcp-r") - 1, "rtcp-r") == 0)
			{
				// a=rtcp-rsize
				auto match = SDPRegexPattern::GetInstance()->MatchRtcpRsize(content.c_str());
				if (match.GetError() == nullptr)
				{
					UseRtcpRsize(true);
				}
			}
			else if (content.compare(0, OV_COUNTOF("rtcp-f") - 1, "rtcp-f") == 0)
			{
				//TODO(Getroot): Implement full spec
				//https://datatracker.ietf.org/doc/html/rfc5104#section-7.1
				// a=rtcp-fb:96 nack pli
				auto match = SDPRegexPattern::GetInstance()->MatchRtcpFb(content.c_str());
				if (match.GetGroupCount() != 2 + 1)
				{
					parsing_error = true;
					break;
				}

				EnableRtcpFb(
					ov::Converter::ToUInt32(match.GetGroupAt(1).GetValue().CStr()),
					match.GetGroupAt(2).GetValue(),
					true);
			}
			else if (content.compare(0, OV_COUNTOF("mid") - 1, "mid") == 0)
			{
				// a=mid:video,
				auto match = SDPRegexPattern::GetInstance()->MatchMid(content.c_str());
				if (match.GetGroupCount() != 1 + 1)
				{
					parsing_error = true;
					break;
				}

				SetMid(match.GetGroupAt(1).GetValue());
			}
			else if (content.compare(0, OV_COUNTOF("msid") - 1, "msid") == 0)
			{
				// a=msid:0nm3jPz5YtRJ1NF26G9IKrUCBlWavuwbeiSf 6jHsvxRPcpiEVZbA5QegGowmCtOlh8kTaXJ4
				auto match = SDPRegexPattern::GetInstance()->MatchMsid(content.c_str());
				if (match.GetGroupCount() != 2 + 1)
				{
					parsing_error = true;
					break;
				}

				SetMsid(
					match.GetGroupAt(1).GetValue(),
					match.GetGroupAt(2).GetValue());
			}
			else if (content.compare(0, OV_COUNTOF("ss") - 1, "ss") == 0)
			{
				// a=ssrc:2064629418 cname:{b2266c86-259f-4853-8662-ea94cf0835a3}
				auto match = SDPRegexPattern::GetInstance()->MatchSsrcCname(content.c_str());
				if (match.GetError() == nullptr)
				{
					if (match.GetGroupCount() != 2 + 1)
					{
						parsing_error = true;
						break;
					}

					if (GetSsrc().has_value())
					{
						// Already set
						break;
					}

					SetSsrc(ov::Converter::ToUInt32(match.GetGroupAt(1).GetValue().CStr()));
					SetCname(match.GetGroupAt(2).GetValue());
				}
				else
				{
					auto match = SDPRegexPattern::GetInstance()->MatchSsrcFid(content.c_str());
					if (match.GetError() == nullptr)
					{
						if (match.GetGroupCount() != 2 + 1)
						{
							parsing_error = true;
							break;
						}

						SetSsrc(ov::Converter::ToUInt32(match.GetGroupAt(1).GetValue().CStr()));
						SetRtxSsrc(ov::Converter::ToUInt32(match.GetGroupAt(2).GetValue().CStr()));
					}
					else
					{
						// unknown pattern
					}
				}
			}
			else if (content.compare(0, OV_COUNTOF("fra") - 1, "fra") == 0)
			{
				// a=framerate:29.97
				auto match = SDPRegexPattern::GetInstance()->MatchFramerate(content.c_str());
				if (match.GetGroupCount() != 1 + 1)
				{
					// Not critical error
					// parsing_error = true;
					logw("SDP", "Sdp parsing error : %c=%s", type, content.c_str());

					break;
				}

				SetFramerate(ov::Converter::ToFloat(match.GetGroupAt(1).GetValue().CStr()));
			}
			else if (content.compare(0, OV_COUNTOF("sen") - 1, "sen") == 0 ||
					 content.compare(0, OV_COUNTOF("rec") - 1, "rec") == 0 ||
					 content.compare(0, OV_COUNTOF("ina") - 1, "ina") == 0)
			{
				// a=sendonly
				// a=recvonly
				// a=sendrecv
				// a=inactive
				auto match = SDPRegexPattern::GetInstance()->MatchDirection(content.c_str());
				if (match.GetGroupCount() != 1 + 1)
				{
					parsing_error = true;
					break;
				}

				SetDirection(match.GetGroupAt(1).GetValue());
			}
			else if (content.compare(0, OV_COUNTOF("fmtp") - 1, "fmtp") == 0)
			{
				// a=fmtp:96 packetization-mode=1;profile-level-id=42e01f;level-asymmetry-allowed=1
				auto match = SDPRegexPattern::GetInstance()->MatchFmtp(content.c_str());
				if (match.GetGroupCount() != 2 + 1)
				{
					parsing_error = true;
					break;
				}

				SetFmtp(
					ov::Converter::ToUInt32(match.GetGroupAt(1).GetValue().CStr()),
					match.GetGroupAt(2).GetValue());
			}
			else if (content.compare(0, OV_COUNTOF("rtcp:") - 1, "rtcp:") == 0)
			{
				// No need
			}
			else if (content.compare(0, OV_COUNTOF("ext") - 1, "ext") == 0)
			{
				// a=extmap:1[/direction] urn:ietf:params:rtp-hdrext:framemarking
				auto match = SDPRegexPattern::GetInstance()->MatchExtmap(content.c_str());
				if (match.GetGroupCount() < 2 + 1)
				{
					break;
				}

				auto value = match.GetGroupAt(1).GetValue();
				uint8_t id = 0;
				if (value.IndexOf("/") == -1)
				{
					id = ov::Converter::ToUInt32(value.CStr());
				}
				else
				{
					id = ov::Converter::ToUInt32(value.Left(value.IndexOf("/")).CStr());
				}

				AddExtmap(
					id,
					match.GetGroupAt(2).GetValue());
			}
			else if (ParsingCommonAttrLine(type, content))
			{
			}
			else
			{
				// Other attributes are ignored because they are not required.
				logt("SDP", "Unknown Attributes : %c=%s", type, content.c_str());
			}

			break;
		default:
			logt("SDP", "Unknown Attributes : %c=%s", type, content.c_str());
			break;
	}

	if (parsing_error)
	{
		logw("SDP", "Sdp parsing error : %c=%s", type, content.c_str());
	}

	return true;
}

// m=video 9 UDP/TLS/RTP/SAVPF 97
void MediaDescription::SetMediaType(const MediaType type)
{
	_media_type = type;
	switch (_media_type)
	{
		case MediaType::Video:
			_media_type_str = "video";
			break;
		case MediaType::Audio:
			_media_type_str = "audio";
			break;
		case MediaType::Application:
			_media_type_str = "application";
			break;
		default:
			_media_type_str = "";
			break;
	}
}

const MediaDescription::MediaType MediaDescription::GetMediaType() const
{
	return _media_type;
}

const ov::String MediaDescription::GetMediaTypeStr() const
{
	return _media_type_str;
}

void MediaDescription::SetPort(uint16_t port)
{
	_port = port;
}

uint16_t MediaDescription::GetPort() const
{
	return _port;
}

void MediaDescription::UseDtls(bool flag)
{
	_use_dtls_flag = flag;

	if (_use_dtls_flag)
	{
		_protocol = "UDP/TLS/RTP/SAVPF";
	}
	else
	{
		_protocol = "RTP/AVPF";
	}
}

bool MediaDescription::IsUseDtls() const
{
	return _use_dtls_flag;
}

void MediaDescription::AddPayload(const std::shared_ptr<PayloadAttr> &payload)
{
	_payload_list.push_back(payload);
}

std::shared_ptr<const PayloadAttr> MediaDescription::GetPayload(uint8_t id) const
{
	for (auto &payload : _payload_list)
	{
		if (payload->GetId() == id)
		{
			return payload;
		}
	}

	return nullptr;
}

std::shared_ptr<PayloadAttr> MediaDescription::GetPayload(uint8_t id)
{
	for (auto &payload : _payload_list)
	{
		if (payload->GetId() == id)
		{
			return payload;
		}
	}

	return nullptr;
}

std::shared_ptr<const PayloadAttr> MediaDescription::GetFirstPayload() const
{
	if (_payload_list.empty() == false)
	{
		return _payload_list.front();
	}

	return nullptr;
}

const std::vector<std::shared_ptr<PayloadAttr>> &MediaDescription::GetPayloadList() const
{
	return _payload_list;
}

bool MediaDescription::AddRid(const ov::String &rid_id, const ov::String &direction, const ov::String &restrictions)
{
	// 1 send pt=100;max-width=1280;max-height=720;max-fps=60;depend=2

	auto rid = std::make_shared<RidAttr>();
	rid->SetId(rid_id);

	if (direction.UpperCaseString() == "SEND")
	{
		rid->SetDirection(RidAttr::Direction::Send);
	}
	else if (direction.UpperCaseString() == "RECV")
	{
		rid->SetDirection(RidAttr::Direction::Recv);
	}
	else
	{
		logte("Unknown direction : %s", direction.CStr());
		return false;
	}

	if (restrictions.IsEmpty() == false)
	{
		auto restrictions_list = restrictions.Split(";");
		for (const auto &restriction : restrictions_list)
		{
			auto key_value = restriction.Split("=");
			if (key_value.size() != 2)
			{
				logte("Unknown restriction : %s", restriction.CStr());
				return false;
			}

			auto key = key_value[0].UpperCaseString();
			auto value = key_value[1];

			rid->AddRestriction(key, value);
		}
	}

	_rid_list.push_back(rid);

	return true;
}

bool MediaDescription::SetSimulcast(const ov::String &send_rids, const ov::String &recv_rids)
{
	// a=simulcast:send 1;2,3 recv 4
	if (send_rids.IsEmpty() == false)
	{
		auto send_rid_list = send_rids.Split(";");
		for (const auto &send_rid : send_rid_list)
		{
			auto send_rids = send_rid.Split(",");
			auto layer = std::make_shared<SimulcastLayer>();

			for (const auto &rid : send_rids)
			{
				// ~rid-id
				if (rid[0] == '~')
				{
					continue;
				}

				auto rid_attr = GetRid(rid);
				if (rid_attr == nullptr)
				{
					logtw("Cannot find rid : %s", rid.CStr());
					continue;
				}

				layer->AddRid(rid_attr);
			}

			AddSendLayerToSimulcast(layer);
		}
	}

	if (recv_rids.IsEmpty() == false)
	{
		auto recv_rid_list = recv_rids.Split(";");
		for (const auto &recv_rid : recv_rid_list)
		{
			auto recv_rids = recv_rid.Split(",");
			auto layer = std::make_shared<SimulcastLayer>();

			for (const auto &rid : recv_rids)
			{
				// ~rid-id
				if (rid[0] == '~')
				{
					continue;
				}

				auto rid_attr = GetRid(rid);
				if (rid_attr == nullptr)
				{
					logtw("Cannot find rid : %s", rid.CStr());
					continue;
				}

				layer->AddRid(rid_attr);
			}

			AddRecvLayerToSimulcast(layer);
		}
	}

	return true;
}

bool MediaDescription::AddSendLayerToSimulcast(const std::shared_ptr<SimulcastLayer> &layer)
{
	_send_layers.push_back(layer);
	return true;
}

bool MediaDescription::AddRecvLayerToSimulcast(const std::shared_ptr<SimulcastLayer> &layer)
{
	_recv_layers.push_back(layer);
	return true;
}

// a=rid:1 send max-width=1280;max-height=720;max-fps=30
// a=simulcast:send 1,2,3;recv 4,5
bool MediaDescription::AddRid(const std::shared_ptr<RidAttr> &rid, bool simulcast, const ov::String &alternative_to_base_rid)
{
	if (simulcast == true)
	{
		if (alternative_to_base_rid.IsEmpty() == false)
		{
			// find base rid from send layers
			// RFC 8853 : To avoid complications in implementations, a single rid-id MUST NOT occur more than once per "a=simulcast" line.
			auto layer = GetSimulcastLayer(alternative_to_base_rid);
			if (layer == nullptr)
			{
				logte("Cannot find base rid : %s", alternative_to_base_rid.CStr());
				return false;
			}

			auto base_rid = layer->GetRid(alternative_to_base_rid);
			if (base_rid->GetDirection() != rid->GetDirection())
			{
				logte("Direction is not matched : %s", alternative_to_base_rid.CStr());
				return false;
			}

			layer->AddRid(rid);
		}
		else
		{
			// add new layer
			auto layer = std::make_shared<SimulcastLayer>();
			layer->AddRid(rid);

			if (rid->GetDirection() == RidAttr::Direction::Send)
			{
				AddSendLayerToSimulcast(layer);
			}
			else
			{
				AddRecvLayerToSimulcast(layer);
			}
		}
	}

	_rid_list.push_back(rid);

	return true;
}

std::shared_ptr<RidAttr> MediaDescription::GetRid(const ov::String &id)
{
	for (auto &rid : _rid_list)
	{
		if (rid->GetId() == id)
		{
			return rid;
		}
	}

	return nullptr;
}

// rid list
std::vector<std::shared_ptr<RidAttr>> MediaDescription::GetRidList() const
{
	return _rid_list;
}

std::shared_ptr<SimulcastLayer> MediaDescription::GetSimulcastLayer(const ov::String &base_rid)
{
	for (auto &layer : _send_layers)
	{
		if (layer->GetRid(base_rid) != nullptr)
		{
			return layer;
		}
	}

	for (auto &layer : _recv_layers)
	{
		if (layer->GetRid(base_rid) != nullptr)
		{
			return layer;
		}
	}

	return nullptr;
}

// simulcast list
std::vector<std::shared_ptr<SimulcastLayer>> MediaDescription::GetSendLayerList() const
{
	return _send_layers;
}

std::vector<std::shared_ptr<SimulcastLayer>> MediaDescription::GetRecvLayerList() const
{
	return _recv_layers;
}

// a=rtcp-mux
void MediaDescription::UseRtcpMux(bool flag)
{
	_use_rtcpmux_flag = flag;
}

bool MediaDescription::IsRtcpMux() const
{
	return _use_rtcpmux_flag;
}

// a=rtcp-rsize
void MediaDescription::UseRtcpRsize(bool flag)
{
	_use_rtcprsize_flag = flag;
}

bool MediaDescription::IsUseRtcpRsize() const
{
	return _use_rtcprsize_flag;
}

// a=sendonly
void MediaDescription::SetDirection(const Direction dir)
{
	_direction = dir;
	switch (_direction)
	{
		case Direction::SendRecv:
			_direction_str = "sendrecv";
			break;
		case Direction::RecvOnly:
			_direction_str = "recvonly";
			break;
		case Direction::SendOnly:
			_direction_str = "sendonly";
			break;
		case Direction::Inactive:
			_direction_str = "inactive";
			break;
		default:
			_direction_str = "";
			break;
	}
}

const MediaDescription::Direction MediaDescription::GetDirection() const
{
	return _direction;
}

// a=mid:video
void MediaDescription::SetMid(const ov::String &mid)
{
	_mid = mid;
}

std::optional<ov::String> MediaDescription::GetMid() const
{
	return _mid;
}

// a=msid:msid appdata
void MediaDescription::SetMsid(const ov::String &msid, const ov::String &msid_appdata)
{
	_msid = msid;
	_msid_appdata = msid_appdata;
}

std::optional<ov::String> MediaDescription::GetMsid()
{
	return _msid;
}

std::optional<ov::String> MediaDescription::GetMsidAppdata()
{
	return _msid_appdata;
}

// c=IN IP4 0.0.0.0
void MediaDescription::SetConnection(uint8_t version, const ov::String &ip)
{
	_connection_ip_version = version;
	_connection_ip = ip;
}

void MediaDescription::SetFramerate(const float framerate)
{
	_framerate = framerate;
}

const float MediaDescription::GetFramerate() const
{
	return _framerate;
}

// a=ssrc:2064629418 cname:{b2266c86-259f-4853-8662-ea94cf0835a3}
void MediaDescription::SetCname(const ov::String &cname)
{
	_cname = cname;
}

void MediaDescription::SetSsrc(uint32_t ssrc)
{
	_ssrc = ssrc;
}

void MediaDescription::SetRtxSsrc(uint32_t rtx_ssrc)
{
	_rtx_ssrc = rtx_ssrc;
}

std::optional<uint32_t> MediaDescription::GetSsrc() const
{
	return _ssrc;
}

std::optional<uint32_t> MediaDescription::GetRtxSsrc() const
{
	return _rtx_ssrc;
}

std::optional<ov::String> MediaDescription::GetCname() const
{
	return _cname;
}

void MediaDescription::AddExtmap(uint8_t id, ov::String attribute)
{
	_extmap[id] = attribute;
}

std::map<uint8_t, ov::String> MediaDescription::GetExtmap() const
{
	return _extmap;
}

ov::String MediaDescription::GetExtmapItem(uint8_t id) const
{
	return _extmap.at(id);
}

bool MediaDescription::FindExtmapItem(const ov::String &keyword, uint8_t &id, ov::String &uri) const
{
	for (auto const &extmap : _extmap)
	{
		auto tmp_id = extmap.first;
		auto tmp_uri = extmap.second;

		if (tmp_uri.IndexOf(keyword.CStr()) != -1)
		{
			id = tmp_id;
			uri = tmp_uri;
			return true;
		}
	}

	return false;
}

// a=rtpmap:96 VP8/50000
bool MediaDescription::AddRtpmap(uint8_t payload_type, const ov::String &codec,
								 uint32_t rate, const ov::String &parameters)
{
	auto payload = GetPayload(payload_type);
	if (payload == nullptr)
	{
		payload = std::make_shared<PayloadAttr>();
		payload->SetId(payload_type);
		AddPayload(payload);
	}

	payload->SetRtpmap(payload_type, codec, rate, parameters);

	return true;
}

// a=rtcp-fb:96 nack pli
bool MediaDescription::EnableRtcpFb(uint8_t id, const ov::String &type, bool on)
{
	std::shared_ptr<PayloadAttr> payload = GetPayload(id);
	if (payload == nullptr)
	{
		payload = std::make_shared<PayloadAttr>();
		payload->SetId(id);
		AddPayload(payload);
	}

	return payload->EnableRtcpFb(type, on);
}

void MediaDescription::SetFmtp(uint8_t id, const ov::String &fmtp)
{
	std::shared_ptr<PayloadAttr> payload = GetPayload(id);
	if (payload == nullptr)
	{
		payload = std::make_shared<PayloadAttr>();
		payload->SetId(id);
		AddPayload(payload);
	}

	return payload->SetFmtp(fmtp);
}

void MediaDescription::EnableRtcpFb(uint8_t id, const PayloadAttr::RtcpFbType &type, bool on)
{
	std::shared_ptr<PayloadAttr> payload = GetPayload(id);
	if (payload == nullptr)
	{
		payload = std::make_shared<PayloadAttr>();
		payload->SetId(id);
		AddPayload(payload);
	}

	payload->EnableRtcpFb(type, on);
}