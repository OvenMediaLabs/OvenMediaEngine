/*
 * We referenced this code from WEBRTC NATIVE CODE.
 */

#pragma once

#include <queue>
#include <vector>

#include "base/common_types.h"
#include "red_rtp_packet.h"

/*
* RTP + RED + FEC
    0                   1                    2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3  4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   | V |P|X|  CC   |M| RED PT(96)  |         SN                    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         timestamp                             |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         SSRC                                  |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ -----------> RTP Header
   |0| FEC PT(97)  |E|L|P|X|  CC   |M| PT recovery |   SN base     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ -----------> 1 Octet for RED
   |   SN base     |               TS recovery                     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  TS recovery  |        length recovery        |   Protection  |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |   Length      |             mask              |  mask cont.   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                mask cont(only when L=1)       |               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+               +
   +                        FEC Payload                            +
   +                                               +---------------+
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

// ULPFEC (RFC 5109) over what a receiver rebuilds from RED: everything after the fixed header, extensions included
// FEC packets follow their frame's last packet; extension values a session stamps per viewer do not come back exactly
class UlpfecGenerator
{
public:
	// `sequence_number` is the one the packet's RED copy is sent with
	void AddMediaPacket(const std::shared_ptr<const RtpPacket> &media_packet, uint16_t sequence_number);
	bool IsAvailableFecPackets() const;
	// Sets the next FEC packet (FEC header and payload) as the payload of `packet`
	bool NextPacket(RtpPacket *packet);

	// Moves the SN base in the FEC packet that `packet` carries as a RED block by `delta`, for a sender that
	// renumbers the stream; a retransmission's payload starts after its OSN, so it works the same
	static bool ShiftSequenceNumberBase(RtpPacket *packet, uint16_t delta);

private:
	struct ProtectedPacket
	{
		std::shared_ptr<const RtpPacket> packet;
		uint16_t sequence_number = 0;
	};

	void Encode();
	std::shared_ptr<ov::Data> MakeFecPacket(const std::vector<const ProtectedPacket *> &protected_packets) const;

	std::vector<ProtectedPacket> _frame_packets;
	std::queue<std::shared_ptr<ov::Data>> _generated_fec_packets;
};
