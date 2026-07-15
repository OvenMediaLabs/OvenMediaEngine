#include <base/ovlibrary/byte_io.h>
#include "rtp_depacketizer_generic_audio.h"

std::shared_ptr<ov::Data> RtpDepacketizerGenericAudio::ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list)
{
	if(payload_list.size() <= 0)
	{
		return nullptr;
	}

	if(payload_list.size() == 1)
	{
		return payload_list.at(0);
	}

	auto reserve_size = 0;
	for(auto &payload : payload_list)
	{
		reserve_size += payload->GetLength();
		reserve_size += 16; // spare
	}

	auto bitstream = std::make_shared<ov::Data>(reserve_size);

	for(const auto &payload : payload_list)
	{
		bitstream->Append(payload);
	}

	return bitstream;
}