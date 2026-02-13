#include <base/ovlibrary/bit_reader.h>
#include "rtp_depacketizer_vp8.h"

std::shared_ptr<ov::Data> RtpDepacketizerVP8::ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list)
{
	if(payload_list.size() <= 0)
	{
		return nullptr;
	}

	auto reserve_size = 0;
	for(auto &payload : payload_list)
	{
		reserve_size += payload->GetLength();
		reserve_size += 16; // spare
	}

	auto bitstream = std::make_shared<ov::Data>(reserve_size);
	bool first_packet = true;
	for(const auto &payload : payload_list)
	{
		auto parsed_data = ParsePayloadDescriptor(payload, first_packet);
		if(parsed_data == nullptr)
		{
			logc("DEBUG", "failed to depacketize VP8");
			return nullptr;
		}

		bitstream->Append(parsed_data);
		first_packet = false;
	}

	return bitstream;
}

std::shared_ptr<ov::Data> RtpDepacketizerVP8::ParsePayloadDescriptor(const std::shared_ptr<ov::Data> &payload, bool first_packet)
{
	BitReader parser(payload->GetDataAs<uint8_t>(), payload->GetLength());

	if(parser.BytesRemained() < 1)
	{
		return nullptr;
	}

	// TODO(Getroot): The parsed value should be used for validation.

	[[maybe_unused]]auto xbit = parser.ReadBoolBit();
	[[maybe_unused]]auto rbit = parser.ReadBoolBit();
	[[maybe_unused]]auto nbit = parser.ReadBoolBit();
	[[maybe_unused]]auto sbit = parser.ReadBoolBit();
	[[maybe_unused]]auto part_id = parser.ReadBits<uint8_t>(4);

	// extension
	if(xbit == true)
	{
		if(parser.BytesRemained() < 1)
		{
			return nullptr;
		}

		[[maybe_unused]] auto ibit = parser.ReadBoolBit();
		[[maybe_unused]] auto lbit = parser.ReadBoolBit();
		[[maybe_unused]] auto tbit = parser.ReadBoolBit();
		[[maybe_unused]] auto kbit = parser.ReadBoolBit();
		[[maybe_unused]] auto rsv = parser.ReadBits<uint8_t>(4);

		if(ibit == true)
		{
			if(parser.BytesRemained() < 1)
			{
				return nullptr;
			}

			[[maybe_unused]]auto mbit = parser.ReadBoolBit();
			if(mbit == true)
			{
				if(parser.BytesRemained() < 1)
				{
					return nullptr;
				}

				[[maybe_unused]] auto pic_id = parser.ReadBits<uint16_t>(15);
			}
			else
			{
				[[maybe_unused]] auto pic_id = parser.ReadBits<uint16_t>(7);
			}
		}

		if(lbit == true)
		{
			if(parser.BytesRemained() < 1)
			{
				return nullptr;
			}

			[[maybe_unused]] auto tl0_pic_idx = parser.ReadBytes<uint8_t>();
		}

		// 1byte present
		if(tbit == true || kbit == true)
		{
			[[maybe_unused]] auto tid = parser.ReadBits<uint8_t>(2);
			[[maybe_unused]] auto y = parser.ReadBit();
			[[maybe_unused]] auto key_idx = parser.ReadBits<uint8_t>(5);

			if(tbit == false)
			{
				// ignore
				tid = 0;
				y = 0;
			}
			else if(kbit == false)
			{
				// ignore
				key_idx = 0;
			}
		}
	}

	if(parser.BytesRemained() < 1)
	{
		return nullptr;
	}

	auto bitstream = std::make_shared<ov::Data>(parser.CurrentPosition(), parser.BytesRemained());

	auto current = parser.CurrentPosition();

	// Check keyframe
	if(first_packet == true && (*current & 0x01) == 0)
	{
		logt("DEBUG", "VP8 Keyframe!");
	}

	return bitstream;
}
