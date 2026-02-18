//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "srtp_transport.h"
#include "dtls_transport.h"
#include <modules/rtsp/rtsp_data.h>

#define OV_LOG_TAG "SRTP"

SrtpTransport::SrtpTransport()
	: ov::Node(NodeType::Srtp)
{

}

SrtpTransport::~SrtpTransport()
{
}

bool SrtpTransport::Stop()
{
	if(_send_session != nullptr)
	{
		_send_session->Release();
	}

	if(_recv_session != nullptr)
	{
		_recv_session->Release();
	}

	for (auto &[channel_id, session] : _channel_recv_sessions)
	{
		if (session != nullptr)
		{
			session->Release();
		}
	}
	_channel_recv_sessions.clear();

	return Node::Stop();
}

bool SrtpTransport::OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data)
{
	if(GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

	if(!_send_session)
	{
		return false;
	}
	
	if(from_node == NodeType::Rtp)
	{
		if(!_send_session->ProtectRtp(data))
		{
			return false;
		}
	}
	else if(from_node == NodeType::Rtcp)
	{
		 if(!_send_session->ProtectRtcp(data))
		 {
			return false;
		 }
	}
	else
	{
		return false;
	}

	// To DTLS transport
	return SendDataToNextNode(data);
}

bool SrtpTransport::OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data)
{
	if(GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

	if(data->GetLength() < 4)
	{
		// Invalid RTP or RTCP packet
		return false;
	}

	// Determine which recv session to use.
	// If per-channel sessions exist (RTSP SDES-SRTP), look up by channel ID.
	// Otherwise fall back to the single _recv_session (WebRTC DTLS-SRTP).
	SrtpAdapter *recv_adapter = nullptr;

	if (!_channel_recv_sessions.empty())
	{
		// Try to extract the interleaved channel ID from RtspData
		auto rtsp_data = std::dynamic_pointer_cast<const RtspData>(data);
		if (rtsp_data != nullptr)
		{
			// Map both RTP (even) and RTCP (odd) channels to the same session
			// The session is keyed by the RTP channel (even)
			uint8_t rtp_channel = rtsp_data->GetChannelId() & ~1;
			auto it = _channel_recv_sessions.find(rtp_channel);
			if (it != _channel_recv_sessions.end())
			{
				recv_adapter = it->second.get();
			}
			else
			{
				logte("No SRTP session found for interleaved channel %u", rtsp_data->GetChannelId());
				return false;
			}
		}
		else
		{
			logte("Per-channel SRTP is configured but received non-RtspData");
			return false;
		}
	}
	else if (_recv_session != nullptr)
	{
		recv_adapter = _recv_session.get();
	}
	else
	{
		return false;
	}

	auto decode_data = data->Clone();
	// Distinguishable RTP and RTCP Packets
	// https://tools.ietf.org/html/rfc5761#section-4
	auto payload_type = decode_data->GetDataAs<uint8_t>()[1];

	NodeType node_type = NodeType::Unknown;

	// RTCP
	if(payload_type >= 192 && payload_type <= 223)
	{
		if(!recv_adapter->UnprotectRtcp(decode_data))
		{
			logtt("RTCP unprotected fail");
			return false;
		}

		node_type = NodeType::Srtcp;
	}	
	// RTP
	else
	{
		if(!recv_adapter->UnprotectRtp(decode_data))
		{
			logtt("RTP unprotected fail");
			return false;
		}

		node_type = NodeType::Srtp;
	}

	// If the original data was RtspData, preserve the channel ID through SRTP decryption
	// so that RtpRtcp can use channel-based track lookup
	auto rtsp_data = std::dynamic_pointer_cast<const RtspData>(data);
	if (rtsp_data != nullptr)
	{
		auto rtsp_decode_data = std::make_shared<RtspData>(rtsp_data->GetChannelId(), decode_data);
		return SendDataToPrevNode(node_type, rtsp_decode_data);
	}

	// To RTP_RTCP
	return SendDataToPrevNode(node_type, decode_data);
}

// Initialize SRTP
bool SrtpTransport::SetKeyMaterial(uint64_t crypto_suite, std::shared_ptr<ov::Data> server_key, std::shared_ptr<ov::Data> client_key)
{
	if(_send_session || _recv_session)
	{
		return false;
	}

	logtt("Try to set key material");

	_send_session = std::make_shared<SrtpAdapter>();
	if(_send_session == nullptr)
	{
		logte("Create srtp adapter failed");
		return false;
	}

	if(!_send_session->SetKey(ssrc_any_outbound, crypto_suite, server_key))
	{
		return false;
	}

	_recv_session = std::make_shared<SrtpAdapter>();
	if(_recv_session == nullptr)
	{
		_send_session.reset();
		_send_session = nullptr;
		return false;
	}

	if(!_recv_session->SetKey(ssrc_any_inbound, crypto_suite, client_key))
	{
		return false;
	}

	return true;
}

bool SrtpTransport::AddChannelKeyMaterial(uint8_t rtp_channel_id, uint64_t crypto_suite, std::shared_ptr<ov::Data> key)
{
	// Ensure the channel ID is even (RTP channel)
	rtp_channel_id = rtp_channel_id & ~1;

	if (_channel_recv_sessions.find(rtp_channel_id) != _channel_recv_sessions.end())
	{
		logte("SRTP session already exists for channel %u", rtp_channel_id);
		return false;
	}

	auto recv_session = std::make_shared<SrtpAdapter>();
	if (recv_session == nullptr)
	{
		logte("Failed to create SRTP adapter for channel %u", rtp_channel_id);
		return false;
	}

	if (!recv_session->SetKey(ssrc_any_inbound, crypto_suite, key))
	{
		logte("Failed to set SRTP key for channel %u", rtp_channel_id);
		return false;
	}

	_channel_recv_sessions[rtp_channel_id] = recv_session;

	logti("Added per-channel SRTP session for interleaved channel %u", rtp_channel_id);

	return true;
}