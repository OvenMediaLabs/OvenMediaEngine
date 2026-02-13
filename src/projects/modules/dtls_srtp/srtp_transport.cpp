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

	if(_recv_session == nullptr)
	{
		return false;
	}

	if(data->GetLength() < 4)
	{
		// Invalid RTP or RTCP packet
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
		if(!_recv_session->UnprotectRtcp(decode_data))
		{
			logtt("RTCP unprotected fail");
			return false;
		}

		node_type = NodeType::Srtcp;
	}	
	// RTP
	else
	{
		if(!_recv_session->UnprotectRtp(decode_data))
		{
			logtt("RTP unprotected fail");
			return false;
		}

		node_type = NodeType::Srtp;
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