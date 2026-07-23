#pragma once

#include <base/ovlibrary/byte_stream.h>

#include "rtp_depacketizing_manager.h"
#include "rtp_rtcp_defines.h"

/*
	AV1 RTP Specification (https://aomediacodec.github.io/av1-rtp-spec/)

	[Aggregation header] (first byte of the payload)

	 0 1 2 3 4 5 6 7
	+-+-+-+-+-+-+-+-+
	|Z|Y| W |N|-|-|-|
	+-+-+-+-+-+-+-+-+

	* Z : first OBU element is a continuation of the previous packet's last OBU element
	* Y : last OBU element will continue in the next packet
	* W : number of OBU elements in the packet (0 = each element has a length field)
	* N : first packet of a new coded video sequence

	When W == 0 every OBU element is preceded by a LEB128 length field. When W is 1..3 the
	first W-1 elements are length-prefixed and the last element runs to the end of the packet.

	OBU elements carry the OBU with obu_has_size_field == 0 (the size is conveyed by the RTP
	framing). To produce a decodable low-overhead bitstream we reassemble fragmented elements
	(Z/Y) and rewrite each OBU with obu_has_size_field == 1 plus a LEB128 obu_size.
*/

class RtpDepacketizerAV1 : public RtpDepacketizingManager
{
public:
	std::shared_ptr<ov::Data> ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list) override;

private:
	// Rewrite one reassembled OBU element (obu_has_size_field == 0) into the low-overhead format
	// (obu_has_size_field == 1 + LEB128 size) and append it to the output stream.
	bool WriteObuWithSizeField(ov::ByteStream &stream, const uint8_t *obu, size_t obu_size);
};
