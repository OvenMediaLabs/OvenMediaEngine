//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/node.h>
#include <base/common_types.h>

#include "modules/rtp_rtcp/rtp_rtcp.h"
#include "srtp_adapter.h"

class SrtpTransport : public ov::Node
{
public:
	SrtpTransport();
	virtual ~SrtpTransport();

	bool Stop() override;

	bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) override;
	bool OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data) override;

	// Single key for all channels (used by WebRTC / DTLS-SRTP)
	bool SetKeyMaterial(uint64_t crypto_suite, std::shared_ptr<ov::Data> server_key, std::shared_ptr<ov::Data> client_key);

	// Per-channel keying for RTSP SDES-SRTP (RFC 4568).
	// Each interleaved channel pair (rtp_channel, rtp_channel+1) gets its own SRTP session.
	bool AddChannelKeyMaterial(uint8_t rtp_channel_id, uint64_t crypto_suite, std::shared_ptr<ov::Data> key);

private:
	// Single-key mode (WebRTC)
	std::shared_ptr<SrtpAdapter>		_send_session = nullptr;
	std::shared_ptr<SrtpAdapter>		_recv_session = nullptr;

	// Per-channel mode (RTSP SDES-SRTP)
	// Maps RTP interleaved channel ID -> recv SrtpAdapter
	// Odd channel IDs (RTCP) are resolved to even channel ID (RTP) via (channel_id & ~1)
	std::map<uint8_t, std::shared_ptr<SrtpAdapter>>	_channel_recv_sessions;
};
