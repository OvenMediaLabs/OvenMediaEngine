//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtc_ice_candidate.h"

#include <utility>

RtcIceCandidate::RtcIceCandidate(const ov::String &transport, const ov::SocketAddress &address, uint16_t sdp_m_line_index, ov::String sdp_mid)
	: IceCandidate(transport, address),

	  _sdp_m_line_index(sdp_m_line_index),
	  _sdp_mid(sdp_mid)
{
}

RtcIceCandidate::RtcIceCandidate(const ov::String &transport, const ov::String &address, int port, uint16_t sdp_m_line_index, ov::String sdp_mid)
	: IceCandidate(transport, address, port),

	  _sdp_m_line_index(sdp_m_line_index),
	  _sdp_mid(sdp_mid)
{

}

RtcIceCandidate::RtcIceCandidate(uint16_t sdp_m_line_index, const ov::String &sdp_mid)
	: _sdp_m_line_index(sdp_m_line_index),
	  _sdp_mid(sdp_mid)
{
}

RtcIceCandidate::~RtcIceCandidate()
{
}

uint16_t RtcIceCandidate::GetSdpMLineIndex() const noexcept
{
	return _sdp_m_line_index;
}

void RtcIceCandidate::SetSdpMLineIndex(uint16_t sdp_m_line_index) noexcept
{
	_sdp_m_line_index = sdp_m_line_index;
}

ov::String RtcIceCandidate::GetSdpMid() const noexcept
{
	return _sdp_mid;
}

void RtcIceCandidate::SetSdpMid(ov::String sdp_mid) noexcept
{
	_sdp_mid = sdp_mid;
}

ov::String RtcIceCandidate::ToString() const noexcept
{
	return ov::String::FormatString(
		"%s typ host generation 0 MLineIndex: %d, Mid: %s",
		IceCandidate::ToString().CStr(),
		_sdp_m_line_index, _sdp_mid.CStr());
}