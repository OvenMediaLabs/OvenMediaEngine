#include "pli.h"
#include "rtcp_private.h"
#include <base/ovlibrary/byte_io.h>

bool PLI::Parse(const RtcpPacket &packet)
{
	// TODO(Getroot)
	// Now, OME doesn't receive PLI packet, only request to webrtc sender
	// It will be implemented later
	return true;
}

// RtcpInfo must provide raw data
std::shared_ptr<ov::Data> PLI::GetData() const 
{
	/*
	//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// 0 |                  SSRC of packet sender                        |
	//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// 4 |                  SSRC of media source                         |
	//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	*/

	std::shared_ptr<ov::Data> pli_message = std::make_shared<ov::Data>();
	pli_message->SetLength(4 + 4);
	ov::ByteStream stream(pli_message.get());

	// Feedback
	stream.WriteBE32(_src_ssrc);
	stream.WriteBE32(_media_ssrc);

	return pli_message;
}

void PLI::DebugPrint()
{

}