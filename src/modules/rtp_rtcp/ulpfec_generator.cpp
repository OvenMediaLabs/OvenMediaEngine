#include "ulpfec_generator.h"

#include <string.h>

#include <algorithm>
#include <array>
#include <bitset>

#include "base/ovlibrary/byte_io.h"

// FEC header and the level 0 header after it (protection length, mask)
constexpr size_t kFecHeaderSize					= 10;
constexpr size_t kProtectionLengthSize			= 2;
constexpr size_t kMaskSizeLBitClear				= 2;
constexpr size_t kMaskSizeLBitSet				= 6;
constexpr size_t kMaxProtectedPacketsLBitClear	= kMaskSizeLBitClear * 8;
constexpr size_t kMaxProtectedPacketsLBitSet	= kMaskSizeLBitSet * 8;

// One FEC packet for this many media packets
constexpr size_t kMediaPacketsPerFecPacket		= 7;
constexpr size_t kMaxFecPacketsPerBlock			= (kMaxProtectedPacketsLBitSet + kMediaPacketsPerFecPacket - 1) / kMediaPacketsPerFecPacket;
static_assert(kMaxFecPacketsPerBlock < 32, "a set of FEC packets is a 32-bit mask");

namespace
{
	// Every non-empty set of `fec_packet_count` FEC packets as a bit mask, smaller sets first
	const std::vector<uint32_t> &ProtectionSets(size_t fec_packet_count)
	{
		static const auto sets_by_count = [] {
			std::array<std::vector<uint32_t>, kMaxFecPacketsPerBlock + 1> table;

			for (size_t count = 1; count <= kMaxFecPacketsPerBlock; count++)
			{
				auto &sets = table[count];

				for (uint32_t set = 1; set < (1u << count); set++)
				{
					sets.push_back(set);
				}

				std::stable_sort(sets.begin(), sets.end(), [](uint32_t left, uint32_t right) {
					return std::bitset<32>(left).count() < std::bitset<32>(right).count();
				});
			}

			return table;
		}();

		return sets_by_count[fec_packet_count];
	}
}  // namespace

void UlpfecGenerator::AddMediaPacket(const std::shared_ptr<const RtpPacket> &media_packet, uint16_t sequence_number)
{
	// A frame that never reached its last packet is left unprotected
	if (media_packet->IsFirstPacketOfFrame())
	{
		_frame_packets.clear();
	}

	_frame_packets.push_back({media_packet, sequence_number});

	if (media_packet->IsLastPacketOfFrame())
	{
		Encode();
		_frame_packets.clear();
	}
}

bool UlpfecGenerator::IsAvailableFecPackets() const
{
	return !_generated_fec_packets.empty();
}

bool UlpfecGenerator::NextPacket(RtpPacket *packet)
{
	if (IsAvailableFecPackets() == false)
	{
		return false;
	}

	auto fec_packet = _generated_fec_packets.front();
	_generated_fec_packets.pop();

	return packet->SetPayload(fec_packet->GetDataAs<uint8_t>(), fec_packet->GetLength());
}

bool UlpfecGenerator::ShiftSequenceNumberBase(RtpPacket *packet, uint16_t delta)
{
	if ((packet == nullptr) || (packet->PayloadSize() < (RED_HEADER_SIZE + kFecHeaderSize)))
	{
		return false;
	}

	uint8_t *fec_header = packet->Payload() + RED_HEADER_SIZE;

	auto sequence_number_base = ByteReader<uint16_t>::ReadBigEndian(&fec_header[2]);
	ByteWriter<uint16_t>::WriteBigEndian(&fec_header[2], static_cast<uint16_t>(sequence_number_base + delta));

	return true;
}

void UlpfecGenerator::Encode()
{
	const size_t frame_packet_count = _frame_packets.size();
	if (frame_packet_count == 0)
	{
		return;
	}

	// A mask reaches 48 packets from its base, so a longer frame is split into even blocks
	const size_t block_count = (frame_packet_count + kMaxProtectedPacketsLBitSet - 1) / kMaxProtectedPacketsLBitSet;
	const size_t block_size	 = (frame_packet_count + block_count - 1) / block_count;

	for (size_t block_start = 0; block_start < frame_packet_count; block_start += block_size)
	{
		const size_t block_end			= std::min(block_start + block_size, frame_packet_count);
		const size_t block_packet_count = block_end - block_start;
		const size_t fec_packet_count	= (block_packet_count + kMediaPacketsPerFecPacket - 1) / kMediaPacketsPerFecPacket;

		// Each packet gets its own set of FEC packets while there are enough sets, so any two lost packets come back;
		// a longer block reuses the sets in turn
		const auto &protection_sets = ProtectionSets(fec_packet_count);

		for (size_t fec_index = 0; fec_index < fec_packet_count; fec_index++)
		{
			std::vector<const ProtectedPacket *> protected_packets;

			for (size_t index = block_start; index < block_end; index++)
			{
				const auto protection_set = protection_sets[(index - block_start) % protection_sets.size()];

				if ((protection_set & (1u << fec_index)) != 0)
				{
					protected_packets.push_back(&_frame_packets[index]);
				}
			}

			auto fec_packet = MakeFecPacket(protected_packets);
			if (fec_packet != nullptr)
			{
				_generated_fec_packets.push(fec_packet);
			}
		}
	}
}

std::shared_ptr<ov::Data> UlpfecGenerator::MakeFecPacket(const std::vector<const ProtectedPacket *> &protected_packets) const
{
	if (protected_packets.empty())
	{
		return nullptr;
	}

	const uint16_t sequence_number_base = protected_packets.front()->sequence_number;
	const uint16_t sequence_number_span = protected_packets.back()->sequence_number - sequence_number_base;
	if (sequence_number_span >= kMaxProtectedPacketsLBitSet)
	{
		return nullptr;
	}

	const bool long_mask		 = sequence_number_span >= kMaxProtectedPacketsLBitClear;
	const size_t mask_size		 = long_mask ? kMaskSizeLBitSet : kMaskSizeLBitClear;
	const size_t fec_header_size = kFecHeaderSize + kProtectionLengthSize + mask_size;

	// What a packet contributes is everything after its fixed header
	size_t protection_length = 0;
	for (const auto protected_packet : protected_packets)
	{
		protection_length = std::max(protection_length, protected_packet->packet->GetDataLength() - FIXED_HEADER_SIZE);
	}

	const size_t fec_packet_length = fec_header_size + protection_length;

	auto fec_packet = std::make_shared<ov::Data>(fec_packet_length);
	fec_packet->SetLength(fec_packet_length);
	auto fec_buffer = fec_packet->GetWritableDataAs<uint8_t>();
	::memset(fec_buffer, 0, fec_packet_length);

	uint8_t *mask = &fec_buffer[kFecHeaderSize + kProtectionLengthSize];

	for (const auto protected_packet : protected_packets)
	{
		const auto &packet			  = protected_packet->packet;
		const uint8_t *buffer		  = packet->Buffer();
		const size_t protected_length = packet->GetDataLength() - FIXED_HEADER_SIZE;

		// P, X, CC recovery and M, PT recovery
		fec_buffer[0] ^= buffer[0];
		fec_buffer[1] ^= buffer[1];

		// TS recovery
		for (size_t index = 4; index < 8; index++)
		{
			fec_buffer[index] ^= buffer[index];
		}

		// Length recovery
		fec_buffer[8] ^= static_cast<uint8_t>(protected_length >> 8);
		fec_buffer[9] ^= static_cast<uint8_t>(protected_length & 0xFF);

		for (size_t index = 0; index < protected_length; index++)
		{
			fec_buffer[fec_header_size + index] ^= buffer[FIXED_HEADER_SIZE + index];
		}

		const uint16_t mask_offset = protected_packet->sequence_number - sequence_number_base;
		mask[mask_offset / 8] |= static_cast<uint8_t>(0x80 >> (mask_offset % 8));
	}

	// E bit clear, L bit by the mask size
	fec_buffer[0] &= 0x3F;
	if (long_mask)
	{
		fec_buffer[0] |= 0x40;
	}

	ByteWriter<uint16_t>::WriteBigEndian(&fec_buffer[2], sequence_number_base);
	ByteWriter<uint16_t>::WriteBigEndian(&fec_buffer[kFecHeaderSize], static_cast<uint16_t>(protection_length));

	return fec_packet;
}
