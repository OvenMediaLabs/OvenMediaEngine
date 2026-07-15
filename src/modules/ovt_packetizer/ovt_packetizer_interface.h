#pragma once

#include "ovt_packet.h"

class OvtPacketizerInterface : public ov::EnableSharedFromThis<OvtPacketizerInterface>
{
public:
	virtual bool 		OnOvtPacketized(std::shared_ptr<OvtPacket> &packet) = 0;
};
