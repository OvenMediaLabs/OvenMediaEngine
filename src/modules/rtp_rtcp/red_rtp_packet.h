#pragma once

#include "rtp_packet.h"

// We use only one block

class RedRtpPacket : public RtpPacket
{
public:
	RedRtpPacket();
	RedRtpPacket(uint8_t red_payload_type, RtpPacket &src);
	RedRtpPacket(RedRtpPacket &src);
	~RedRtpPacket();

	void 		PackageAsRed(uint8_t red_payload_type);
	// Readjust the payload area that was adjusted for RED.
	void		PackageAsRtp();
	uint8_t 	BlockPT() const;

private:
	uint8_t		_block_pt;
};

