#include "ulpfec_generator.h"
#include "base/ovlibrary/byte_io.h"

#include <string.h>

constexpr size_t 	kFecHeaderSize					= 10;
constexpr size_t 	kMaskSizeLbitClear				= 2;
constexpr size_t	kMaskSizeLbitSet				= 6;
constexpr size_t 	kFecLevelHeaderSizeLbitClear 	= 2 + kMaskSizeLbitClear;
constexpr size_t	kFecLevelHeaderSizeLbitSet		= 2 + kMaskSizeLbitSet;
constexpr size_t	kUlpfecMaxMediaPacketsLbitClear	= 16;

constexpr size_t    kMediaPacketNumMakeFec          = 7; 

UlpfecGenerator::UlpfecGenerator()
{
	_high_level = false;
}

UlpfecGenerator::~UlpfecGenerator()
{
}

void UlpfecGenerator::SetHighRateProtection(bool high_level)
{
	_high_level = high_level;
}

bool UlpfecGenerator::AddRtpPacketAndGenerateFec(std::shared_ptr<RedRtpPacket> packet)
{
	auto copy_packet = std::make_shared<RedRtpPacket>(*packet);
	_media_packets.push_back(copy_packet);

	if(copy_packet->Marker())
	{
		Encode();
	}

	return true;
}
bool UlpfecGenerator::IsAvailableFecPackets() const
{
	return !_generated_fec_packets.empty();
}

bool UlpfecGenerator::NextPacket(RtpPacket *packet)
{
	if(!IsAvailableFecPackets())
	{
		return false;
	}

	// First FEC
	auto fec_packet = _generated_fec_packets.front();
	_generated_fec_packets.pop();

	return packet->SetPayload(fec_packet->GetDataAs<uint8_t>(), fec_packet->GetLength());
}

bool UlpfecGenerator::Encode()
{
	size_t media_size = _media_packets.size();
	//uint32_t rate = static_cast<uint32_t>(kMediaPacketNumMakeFec / (_high_level?2:1));
	uint32_t fec_packet_count = static_cast<uint32_t>(std::ceil((float)media_size / (float)kMediaPacketNumMakeFec));
	uint32_t media_packet_idx = 0;
	size_t mask_len = 0;

	size_t fec_header_size = kFecHeaderSize;
	if(_media_packets.size() <= kUlpfecMaxMediaPacketsLbitClear)
	{
		fec_header_size += kFecLevelHeaderSizeLbitClear;
		mask_len = kMaskSizeLbitClear;
	}
	else
	{
		fec_header_size += kFecLevelHeaderSizeLbitSet;
		mask_len = kMaskSizeLbitSet;
	}

	for(uint32_t i=0; i<fec_packet_count; i++)
	{
		uint16_t sn_base = 0;
		bool first_media_packet;
		uint8_t mask[6];
		int selected_media_count;

		first_media_packet = true;

		// TODO(Getroot): A more efficient algorithm should be used like random mask or bursty mask.
		auto fec_packet = std::make_shared<ov::Data>();
		fec_packet->SetLength(fec_header_size);
		auto fec_buffer = fec_packet->GetWritableDataAs<uint8_t>();

		// Initialize
		memset(mask, 0, sizeof(mask));
		first_media_packet = true;
		selected_media_count = (media_size-media_packet_idx) / (fec_packet_count - i);

		for(int j=0; j<selected_media_count; j++)
		{
			auto media_packet = _media_packets[media_packet_idx];

			size_t fec_packet_length = fec_header_size + media_packet->PayloadSize();

			if(fec_packet->GetLength() < fec_packet_length)
			{
				fec_packet->SetLength(fec_packet_length);
				fec_buffer = fec_packet->GetWritableDataAs<uint8_t>();
			}

			if(first_media_packet)
			{
				// Write P, X, CC fields.
				// Bits 0, 1 are overwritten in FinalizeFecHeaders.
				fec_buffer[0] = media_packet->Buffer()[0];

				// The media_packet is red packet. So buffer[1] of RTP header has red payload type.
				// We should use media payload type in the red header.
				// M, and PT recovery
				fec_buffer[1] = media_packet->Buffer()[media_packet->HeadersSize() - 1];
				if(media_packet->Marker())
				{
					fec_buffer[1] |= 0x80;
				}
				else
				{
					fec_buffer[1] &= 0x7F;
				}

				// SN Base
				ByteWriter<uint16_t>::WriteBigEndian(&fec_buffer[2], media_packet->SequenceNumber());
				// Write timestamp recovery field.
				ByteWriter<uint32_t>::WriteBigEndian(&fec_buffer[4], media_packet->Timestamp());
				// Write length recovery field.
				ByteWriter<uint16_t>::WriteBigEndian(&fec_buffer[8], (uint16_t)media_packet->PayloadSize());
				// Write Payload.
				memcpy(&fec_buffer[fec_header_size], media_packet->Payload(), media_packet->PayloadSize());

				sn_base = media_packet->SequenceNumber();

				first_media_packet = false;
			}
			else
			{
				XorFecPacket(fec_buffer, fec_header_size, media_packet.get());
			}

			uint16_t diff = media_packet->SequenceNumber() - sn_base;
			mask[diff / 8] |= 1 << (7 - (diff % 8));
			media_packet_idx++;
		}

		FinalizeFecHeader(fec_buffer, fec_packet->GetLength() - fec_header_size, mask, mask_len);

		_generated_fec_packets.push(fec_packet);
	}
	// clear media packet
	_media_packets.clear();

	return true;
}

void UlpfecGenerator::XorFecPacket(uint8_t *fec_packet, size_t fec_header_len, RedRtpPacket *media_packet)
{
	auto rtp_header = media_packet->Header();
	auto rtp_header_len = media_packet->HeadersSize();
	auto rtp_payload = media_packet->Payload();
	auto rtp_payload_len = media_packet->PayloadSize();

	// XOR the first 2 bytes of the header: V, P, X, CC
	fec_packet[0] ^= rtp_header[0];

	uint8_t m_pt_fields = rtp_header[rtp_header_len-1];
	if(media_packet->Marker())
	{
		m_pt_fields |= 0x80;
	}
	else
	{
		m_pt_fields &= 0x7F;
	}
	fec_packet[1] ^= m_pt_fields;

	// XOR TS recovery
	fec_packet[4] ^= rtp_header[4];
	fec_packet[5] ^= rtp_header[5];
	fec_packet[6] ^= rtp_header[6];
	fec_packet[7] ^= rtp_header[7];

	// XOR Length recovery
	uint8_t rtp_payload_length_network_order[2];
	ByteWriter<uint16_t>::WriteBigEndian(rtp_payload_length_network_order, (uint16_t)rtp_payload_len);
	fec_packet[8] ^= rtp_payload_length_network_order[0];
	fec_packet[9] ^= rtp_payload_length_network_order[1];

	// XOR Payload
	for(size_t i = 0; i < rtp_payload_len; i++)
	{
		fec_packet[fec_header_len + i] ^= rtp_payload[i];
	}
}

void UlpfecGenerator::FinalizeFecHeader(uint8_t *fec_packet, const size_t fec_payload_len, const uint8_t *mask, const size_t mask_len)
{
	// Set E bit to zero.
	fec_packet[0] &= 0x7f;

	// Set L bit
	if(_media_packets.size() <= kUlpfecMaxMediaPacketsLbitClear)
	{
		// Clear L bit
		fec_packet[0] &= 0xbf;
	}
	else
	{
		// Set L bit
		fec_packet[0] |= 0x40;
	}

	// FEC Level header

	// Protection Length
	ByteWriter<uint16_t>::WriteBigEndian(&fec_packet[10], static_cast<uint16_t>(fec_payload_len));

	// Mask
	memcpy(&fec_packet[12], mask, mask_len);
}