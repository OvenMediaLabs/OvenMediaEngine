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

	bool SetKeyMaterial(uint64_t crypto_suite, std::shared_ptr<ov::Data> server_key, std::shared_ptr<ov::Data> client_key);

private:
	std::shared_ptr<SrtpAdapter>		_send_session = nullptr;
	std::shared_ptr<SrtpAdapter>		_recv_session = nullptr;
};
