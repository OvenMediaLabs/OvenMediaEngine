//==============================================================================
//
//  StunClient
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/socket.h>

class StunClient
{
public:
	static bool GetMappedAddress(const ov::SocketAddress &stun_server, ov::SocketAddress &mapped_address);

private:
	
};