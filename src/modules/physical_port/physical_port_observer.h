//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>

#include <memory>

class PhysicalPort;

enum class PhysicalPortDisconnectReason
{
	/// An error occurred
	Error,
	/// Disconnected by server
	Disconnect,
	/// Disconnected from client
	Disconnected,
};

enum class PhysicalPortProbeResult
{
	Failed,
	Success,
	NeedMoreData,
};

class PhysicalPortObserver
{
public:
	friend class PhysicalPort;

	virtual ~PhysicalPortObserver() = default;

	// Called when the client is connected
	virtual void OnConnected(const std::shared_ptr<ov::Socket> &remote) {}

	// Called when the packet is received (Only used when TCP/SRT)
	virtual void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) {}

	// Called when the packet is received (Only used when UDP)
	virtual void OnDatagramReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, const std::shared_ptr<const ov::Data> &data) {}

	// Called when the client is disconnected
	virtual void OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error) {}

	// When data is received, the PhysicalPort asks if it is your data
	virtual PhysicalPortProbeResult ProbePacket(const std::shared_ptr<ov::Socket> &remote, const std::shared_ptr<const ov::Data> &data)
	{
		// stub code
		return PhysicalPortProbeResult::Failed;
	}
};
