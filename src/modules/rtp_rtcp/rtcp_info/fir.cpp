#include "fir.h"
#include "rtcp_private.h"
#include <base/ovlibrary/byte_io.h>

bool FIR::Parse(const RtcpPacket &packet)
{
	// TODO(Getroot)
	// Now, OME doesn't receive FIR packet, only request to webrtc sender
	// It will be implemented later
	return true;
}

// RtcpInfo must provide raw data
std::shared_ptr<ov::Data> FIR::GetData() const 
{
	/*
	//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// 0 |                  SSRC of packet sender                        |
	//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// 4 |                  SSRC of media source                         |
	//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	//   :            Feedback Control Information (FCI)                 :
	//   :                                                               :
	*/

	if(GetFirMessageCount() == 0)
	{
		return nullptr;
	}

	std::shared_ptr<ov::Data> fir_message = std::make_shared<ov::Data>();
	fir_message->SetLength(4 + 4 + (8*GetFirMessageCount()));
	ov::ByteStream stream(fir_message.get());

	// Feedback
	stream.WriteBE32(_src_ssrc);
	stream.WriteBE32(_media_ssrc);

	// FCI
	for(const auto &message : _fir_message)
	{
		stream.WriteBE32(message.first);
		stream.Write8(message.second);
		stream.Write24(0);
	}

	return fir_message;
}

void FIR::DebugPrint()
{

}