#pragma once

#include <base/mediarouter/media_buffer.h>
#include "ovt_packet.h"
#include "ovt_packetizer_interface.h"

class OvtPacketizer
{
public:
	// Get method
	OvtPacketizer();
	// Callback method
	OvtPacketizer(const std::shared_ptr<OvtPacketizerInterface> &stream);
	~OvtPacketizer();

	bool PacketizeMessage(uint8_t payload_type, uint64_t timestamp, const std::shared_ptr<ov::Data> &message);
	// Packetizing the MediaPacket
	bool PacketizeMediaPacket(uint64_t timestamp, const std::shared_ptr<MediaPacket> &media_packet);

	bool IsAvailablePackets();
	std::shared_ptr<OvtPacket> PopPacket();

	void Release()
	{
		_stream.reset();
	}

private:
	uint16_t 									_sequence_number;
	std::shared_ptr<OvtPacketizerInterface> 	_stream = nullptr;
	
	std::queue<std::shared_ptr<OvtPacket>>		_ovt_packets;
};
