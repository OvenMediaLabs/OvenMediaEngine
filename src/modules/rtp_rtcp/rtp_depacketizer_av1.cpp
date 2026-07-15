#include "rtp_depacketizer_av1.h"

#include <modules/bitstream/av1/av1_parser.h>

#define OV_LOG_TAG "RtpDepacketizerAV1"

// Aggregation header bit masks (AV1 RTP Specification).
#define AV1_AGG_Z 0x80
#define AV1_AGG_Y 0x40
#define AV1_AGG_W 0x30
#define AV1_AGG_W_SHIFT 4

// obu_header: obu_has_size_field is bit 1 (forbidden|type(4)|extension_flag|has_size_field|reserved).
#define AV1_OBU_HAS_SIZE_FIELD 0x02

std::shared_ptr<ov::Data> RtpDepacketizerAV1::ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list)
{
	if (payload_list.empty())
	{
		return nullptr;
	}

	// Step 1: reassemble OBU elements across packets. Z continues the previous packet's last
	// element; Y holds the current element open for the next packet. Elements are stored without
	// a size field, exactly as carried over RTP.
	std::vector<std::shared_ptr<ov::Data>> obu_elements;
	std::shared_ptr<ov::Data> current = nullptr;
	size_t reserve_size = 0;

	for (const auto &payload : payload_list)
	{
		const uint8_t *data = payload->GetDataAs<uint8_t>();
		const size_t size = payload->GetLength();
		reserve_size += size + 8;	// + LEB128/size-field overhead headroom

		if (size < 1)
		{
			logte("Empty AV1 RTP payload");
			return nullptr;
		}

		const uint8_t agg = data[0];
		const bool z = (agg & AV1_AGG_Z) != 0;
		const bool y = (agg & AV1_AGG_Y) != 0;
		const uint8_t w = (agg & AV1_AGG_W) >> AV1_AGG_W_SHIFT;

		size_t offset = 1;
		size_t index = 0;
		while (offset < size)
		{
			size_t elem_len;
			if (w == 0 || index + 1 < w)
			{
				// Length-prefixed OBU element.
				auto leb = Av1Parser::DecodeLeb128(data + offset, size - offset);
				if (leb.has_value() == false)
				{
					logte("Malformed AV1 OBU element length");
					return nullptr;
				}

				offset += leb->bytes_consumed;
				if (leb->value > size - offset)
				{
					logte("AV1 OBU element length exceeds packet");
					return nullptr;
				}

				elem_len = static_cast<size_t>(leb->value);
			}
			else
			{
				// W>0: the last (W-th) element runs to the end of the packet.
				elem_len = size - offset;
			}

			const uint8_t *elem_ptr = data + offset;
			offset += elem_len;

			const bool first_in_packet = (index == 0);
			const bool last_in_packet = (w == 0) ? (offset == size) : (index + 1 == w);

			if (first_in_packet && z)
			{
				// Continuation of the previous packet's last element.
				if (current == nullptr)
				{
					logte("AV1 Z bit set but no fragment in progress");
					return nullptr;
				}
				current->Append(elem_ptr, elem_len);
			}
			else
			{
				// A fresh element starts. Finalize a stale fragment, if any (defensive: a
				// previous Y that the next packet did not actually continue).
				if (current != nullptr)
				{
					obu_elements.push_back(current);
				}
				current = std::make_shared<ov::Data>(elem_ptr, elem_len);
			}

			if (last_in_packet && y)
			{
				// Continues into the next packet; keep accumulating into `current`.
			}
			else
			{
				obu_elements.push_back(current);
				current = nullptr;
			}

			index++;
			if (w != 0 && index == w)
			{
				break;
			}
		}

		// W (when non-zero) is the exact OBU element count; a short packet that yields fewer is malformed.
		if (w != 0 && index != w)
		{
			logte("AV1 aggregation header declares W=%u but %zu OBU element(s) present", w, index);
			return nullptr;
		}
	}

	// A trailing Y with no continuation packet: finalize what we have.
	if (current != nullptr)
	{
		obu_elements.push_back(current);
		current = nullptr;
	}

	if (obu_elements.empty())
	{
		return nullptr;
	}

	// Step 2: rewrite each OBU into the low-overhead bitstream format (obu_has_size_field == 1).
	auto bitstream = std::make_shared<ov::Data>(reserve_size);
	ov::ByteStream stream(bitstream);

	for (const auto &element : obu_elements)
	{
		// A zero-length OBU element is non-conformant; skip just that element so one stray
		// entry does not discard the whole temporal unit.
		if (element->GetLength() == 0)
		{
			continue;
		}

		if (WriteObuWithSizeField(stream, element->GetDataAs<uint8_t>(), element->GetLength()) == false)
		{
			logte("Failed to rewrite AV1 OBU");
			return nullptr;
		}
	}

	if (bitstream->GetLength() == 0)
	{
		return nullptr;
	}

	return bitstream;
}

bool RtpDepacketizerAV1::WriteObuWithSizeField(ov::ByteStream &stream, const uint8_t *obu, size_t obu_size)
{
	if (obu_size < 1)
	{
		return false;
	}

	auto parsed = Av1Parser::ParseObuHeader(obu, obu_size);
	if (parsed.has_value() == false)
	{
		return false;
	}

	const size_t header_size = parsed->bytes_consumed;	// 1 byte, or 2 with the extension header

	// Already low-overhead (size field present): copy verbatim.
	if (parsed->header.has_size_field)
	{
		return stream.Write(obu, obu_size);
	}

	const size_t payload_size = obu_size - header_size;

	// Set obu_has_size_field on the first header byte; the extension byte (if any) is unchanged.
	const uint8_t header0 = static_cast<uint8_t>(obu[0] | AV1_OBU_HAS_SIZE_FIELD);
	if (stream.WriteBE(header0) == false)
	{
		return false;
	}
	if (header_size == 2)
	{
		if (stream.WriteBE(obu[1]) == false)
		{
			return false;
		}
	}

	uint8_t leb[Av1Parser::LEB128_MAX_SIZE];
	const size_t leb_len = Av1Parser::EncodeLeb128(payload_size, leb);
	if (leb_len == 0)
	{
		return false;
	}
	if (stream.Write(leb, leb_len) == false)
	{
		return false;
	}

	if (payload_size > 0)
	{
		if (stream.Write(obu + header_size, payload_size) == false)
		{
			return false;
		}
	}

	return true;
}
