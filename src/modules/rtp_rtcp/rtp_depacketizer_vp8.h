#pragma once

#include "rtp_depacketizing_manager.h"
#include "rtp_rtcp_defines.h"

/* 
	https://tools.ietf.org/html/rfc7741

	[RTP Packet]

	  0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |V=2|P|X|  CC   |M|     PT      |       sequence number         |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                           timestamp                           |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |           synchronization source (SSRC) identifier            |
     +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
     |            contributing source (CSRC) identifiers             |
     |                             ....                              |
     +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
     |            VP8 payload descriptor (integer #octets)           |
     :                                                               :
     |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                               : VP8 payload header (3 octets) |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     | VP8 pyld hdr  :                                               |
     +-+-+-+-+-+-+-+-+                                               |
     :                   Octets 4..N of VP8 payload                  :
     |                                                               |
     |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                               :    OPTIONAL RTP padding       |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

	[VP8 Payload Descriptor]

	     0 1 2 3 4 5 6 7                     0 1 2 3 4 5 6 7
        +-+-+-+-+-+-+-+-+                   +-+-+-+-+-+-+-+-+
        |X|R|N|S|R| PID | (REQUIRED)        |X|R|N|S|R| PID | (REQUIRED)
        +-+-+-+-+-+-+-+-+                   +-+-+-+-+-+-+-+-+
   X:   |I|L|T|K| RSV   | (OPTIONAL)   X:   |I|L|T|K| RSV   | (OPTIONAL)
        +-+-+-+-+-+-+-+-+                   +-+-+-+-+-+-+-+-+
   I:   |M| PictureID   | (OPTIONAL)   I:   |M| PictureID   | (OPTIONAL)
        +-+-+-+-+-+-+-+-+                   +-+-+-+-+-+-+-+-+
   L:   |   TL0PICIDX   | (OPTIONAL)        |   PictureID   |
        +-+-+-+-+-+-+-+-+                   +-+-+-+-+-+-+-+-+
   T/K: |TID|Y| KEYIDX  | (OPTIONAL)   L:   |   TL0PICIDX   | (OPTIONAL)
        +-+-+-+-+-+-+-+-+                   +-+-+-+-+-+-+-+-+
                                       T/K: |TID|Y| KEYIDX  | (OPTIONAL)
                                            +-+-+-+-+-+-+-+-+
		<Single-octet Pic ID>				<Dual-octet Pic ID>
		* M bit = 0							* M bit = 1

	* X : Extened control bit present
	* R : Reserved
	* N : Non-reference frame
	* S : Start of VP8 partition (First packet)
	* PID : Partition index (First packet = 0, <= 7)

	* I : Picture ID present
	* L : TL0PICIDX present
	* T : TID present
	* K : KEYIDX present
	* RSV : Reserved

	[VP8 Payload Header]
		                    0 1 2 3 4 5 6 7
                            +-+-+-+-+-+-+-+-+
                            |Size0|H| VER |P|
                            +-+-+-+-+-+-+-+-+
                            |     Size1     |
                            +-+-+-+-+-+-+-+-+
                            |     Size2     |
                            +-+-+-+-+-+-+-+-+
                            | Octets 4..N of|
                            | VP8 payload   |
                            :               :
                            +-+-+-+-+-+-+-+-+
                            | OPTIONAL RTP  |
                            | padding       |
                            :               :
                            +-+-+-+-+-+-+-+-+
	* P - 0 : Keyframe, 1 : Delta


*/

class RtpDepacketizerVP8 : public RtpDepacketizingManager
{
public:
	std::shared_ptr<ov::Data> ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list) override;

private:
	// Parse VP8 payload descriptor and return payload
	std::shared_ptr<ov::Data> ParsePayloadDescriptor(const std::shared_ptr<ov::Data> &payload, bool first_packet);
};