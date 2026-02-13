//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "decoder.h"

#include <base/ovlibrary/bit_reader.h>
#include "huffman_codec.h"
#include "hpack_private.h"

namespace http
{
	namespace hpack
	{
		bool Decoder::Decode(const std::shared_ptr<const ov::Data> &data, std::vector<HeaderField> &header_fields)
		{
			std::lock_guard<std::mutex> lock(_decoder_lock);
			auto reader = std::make_shared<BitReader>(data->GetDataAs<uint8_t>(), data->GetLength());

			while (reader->BytesRemained() > 0)
			{
				// Check if the next is the Indexed Header Field
				// Start with 0b1
				auto b = reader->ReadBit();
				if (b == 1)
				{
					if (DecodeIndexedHeaderField(reader, header_fields) == false)
					{
						return false;
					}

					continue;
				}

				// Check if the next is the Literal Header Field with Incremental Indexing
				// Start with 0b01
				b = reader->ReadBit();
				if (b == 1)
				{
					if (DecodeLiteralHeaderFieldWithIndexing(reader, header_fields) == false)
					{
						return false;
					}

					continue;
				}

				// Check if the next is the Dynamic Table Size Update
				// Start with 0b001
				b = reader->ReadBit();
				if (b == 1)
				{
					// Dynamic Table Size Update
					if (DecodeDynamicTableSizeUpdate(reader) == false)
					{
						return false;
					}

					continue;
				}

				// Check if the next is the Literal Header Field without Indexing
				// Start with 0b0000
				b = reader->ReadBit();
				if (b == 0)
				{
					if (DecodeLiteralHeaderFieldWithoutIndexing(reader, header_fields) == false)
					{
						return false;
					}

					continue;
				}
				// Check if the next is the Literal Header Field never Indexed
				// Start with 0b0001
				else if (b == 1)
				{
					if (DecodeLiteralHeaderFieldNeverIndexed(reader, header_fields) == false)
					{
						return false;
					}

					continue;
				}
			}

			return true;
		}

		bool Decoder::DecodeIndexedHeaderField(const std::shared_ptr<BitReader> &reader, std::vector<HeaderField> &header_fields)
		{
			// Indexed Header Field Representation
			uint64_t index = reader->ReadBits<uint8_t>(7);
			if (index == 0)
			{
				// Error
				return false;
			}
			else if (index == 0x7F)
			{
				uint64_t extra;
				if (ReadULEB128(reader, extra) == false)
				{
					return false;
				}
				index += extra;
			}

			// Find and return header field
			HeaderField header_field;
			if (_table_connector.GetHeaderField(index, header_field) == true)
			{
				header_fields.push_back(header_field);
				logtt("DecodeIndexedHeaderField: %s", header_field.ToString().CStr());
				return true;
			}

			return false;
		}

		bool Decoder::DecodeLiteralHeaderFieldWithIndexing(const std::shared_ptr<BitReader> &reader, std::vector<HeaderField> &header_fields)
		{
			HeaderField header_field;
			if (ReadLiteralHeaderField(6, reader, header_field) == false)
			{
				return false;
			}

			header_fields.push_back(header_field);

			// Indexing decoded Header Field
			_table_connector.Index(header_field);

			logtt("DecodeLiteralHeaderFieldWithIndexing: %s", header_field.ToString().CStr());

			return true;
		}

		bool Decoder::DecodeLiteralHeaderFieldWithoutIndexing(const std::shared_ptr<BitReader> &reader, std::vector<HeaderField> &header_fields)
		{
			HeaderField header_field;
			if (ReadLiteralHeaderField(4, reader, header_field) == false)
			{
				return false;
			}

			header_fields.push_back(header_field);

			logtt("DecodeLiteralHeaderFieldWithoutIndexing: %s", header_field.ToString().CStr());

			return true;
		}

		bool Decoder::DecodeLiteralHeaderFieldNeverIndexed(const std::shared_ptr<BitReader> &reader, std::vector<HeaderField> &header_fields)
		{
			HeaderField header_field;
			if (ReadLiteralHeaderField(4, reader, header_field) == false)
			{
				return false;
			}

			header_fields.push_back(header_field);

			logtt("DecodeLiteralHeaderFieldNeverIndexed: %s", header_field.ToString().CStr());

			return true;
		}
		
		bool Decoder::DecodeDynamicTableSizeUpdate(const std::shared_ptr<BitReader> &reader)
		{
			uint64_t size = reader->ReadBits<uint8_t>(5);
			if (size == 0)
			{
				// Error
				return false;
			}
			else if (size == 0x1F)
			{
				uint64_t extra = 0;
				if (ReadULEB128(reader, extra) == false)
				{
					return false;
				}
				size += extra;
			}

			_table_connector.UpdateDynamicTableSize(size);

			logtt("DecodeDynamicTableSizeUpdate: %d", size);

			return true;
		}
		
		bool Decoder::ReadLiteralHeaderField(uint8_t index_bits, const std::shared_ptr<BitReader> &reader, HeaderField &header_field)
		{
			// Indexed Header Field Representation
			uint64_t index = reader->ReadBits<uint8_t>(index_bits);
			
			if (index == (std::pow(2, index_bits) - 1))
			{
				uint64_t extra = 0;
				if (ReadULEB128(reader, extra) == false)
				{
					return false;
				}
				index += extra;
			}

			ov::String name, value;

			// Read Name
			if (index == 0x00)
			{
				if (ReadString(reader, name) == false)
				{
					return false;
				}
			}
			else
			{
				HeaderField header_field;
				if (_table_connector.GetHeaderField(index, header_field) == true)
				{
					name = header_field.GetName();
				}
				else
				{
					// Error
					return false;
				}
			}

			// Read Value
			if (ReadString(reader, value) == false)
			{
				return false;
			}

			header_field.SetNameValue(name, value);

			return true;
		}

		bool Decoder::ReadULEB128(const std::shared_ptr<BitReader> &reader, uint64_t &value)
		{
			// https://www.rfc-editor.org/rfc/rfc7541.html#section-5.1
			// decode I from the next N bits
			// if I < 2^N - 1, return I
			// else
			// 	M = 0
			// 	repeat
			// 		B = next octet
			// 		I = I + (B & 127) * 2^M
			// 		M = M + 7
			// 	while B & 128 == 128
			// 	return I

			// For a more detailed explanation, see the Wiki. https://en.wikipedia.org/wiki/LEB128
			
			value = 0;
			uint64_t shift = 0;
			do 
			{
				auto b = reader->ReadBytes<uint8_t>();
				value |= (b & 0x7F) << shift;
				shift += 7;

				if ((b & 0x80) == 0)
				{
					return true;
				}

			} while(reader->BytesRemained() > 0);

			return false;
		}

		bool Decoder::ReadString(const std::shared_ptr<BitReader> &reader, ov::String &value)
		{
			// https://www.rfc-editor.org/rfc/rfc7541.html#section-5.2
			//   0   1   2   3   4   5   6   7
			// +---+---+---+---+---+---+---+---+
			// | H |    String Length (7+)     |
			// +---+---------------------------+
			// |  String Data (Length octets)  |
   			// +-------------------------------+

			// read H
			// Huffman Encoded String
			auto h = reader->ReadBit();

			// Read Length
			uint64_t length = reader->ReadBits<uint8_t>(7);
			if (length == 0)
			{
				// Error
				return false;
			}
			else if (length == 0x7F)
			{
				uint64_t extra = 0;
				if (ReadULEB128(reader, extra) == false)
				{
					return false;
				}

				length += extra;
			}

			if (h == 1)
			{
				auto encoded_data = std::make_shared<ov::Data>(reader->CurrentPosition(), length);
				reader->SkipBytes(length);
				// Decode Huffman Encoded String
				if (HuffmanCodec::GetInstance()->Decode(encoded_data, value) == false)
				{
					return false;
				}
			}
			else
			{
				value = reader->ReadString(length);
			}

			return true;
		}

	}  // namespace hpack
}  // namespace http