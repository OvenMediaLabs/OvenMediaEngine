//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "modules/ice/ice_candidate.h"

class RtcIceCandidate : public IceCandidate
{
public:
	RtcIceCandidate(const ov::String &transport, const ov::SocketAddress &address, uint16_t sdp_m_line_index, ov::String sdp_mid);
	RtcIceCandidate(const ov::String &transport, const ov::String &address, int port, uint16_t sdp_m_line_index, ov::String sdp_mid);

	// using for parsing
	RtcIceCandidate(uint16_t sdp_m_line_index, const ov::String &sdp_mid);
	~RtcIceCandidate() override;

	RtcIceCandidate &operator=(const RtcIceCandidate &candidate) = default;

	uint16_t GetSdpMLineIndex() const noexcept;
	void SetSdpMLineIndex(uint16_t sdp_m_line_index) noexcept;

	ov::String GetSdpMid() const noexcept;
	void SetSdpMid(ov::String sdp_mid) noexcept;

	ov::String ToString() const noexcept override;

protected:
	uint16_t _sdp_m_line_index;
	ov::String _sdp_mid;
};

// To use ICE Candidate in rotation, keep the grouped list by port
// Initially grouped by port, then ICE Candidates generated from that port are stored in the vector
using RtcIceCandidateList = std::vector<std::vector<RtcIceCandidate>>;
