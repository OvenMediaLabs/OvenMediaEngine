//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder.h"
#include "huffman_codec.h"
#include "hpack_private.h"

namespace http
{
	namespace hpack
	{
		bool Encoder::UpdateDynamicTableSize(size_t size)
		{
			std::lock_guard<std::mutex> lock(_encoder_lock);
			if (_table_connector.UpdateDynamicTableSize(size) == false)
			{
				return false;
			}

			_need_signal_table_size_update = true;
			return true;
		}

		std::shared_ptr<ov::Data> Encoder::Encode(const HeaderField &header_fields, EncodingType type)
		{
			std::lock_guard<std::mutex> lock(_encoder_lock);

			std::shared_ptr<ov::Data> encoded_data = std::make_shared<ov::Data>(header_fields.GetSize());
			ov::ByteStream stream(encoded_data.get());

			bool result = false;

			if (_need_signal_table_size_update)
			{
				if (EncodeDynamicTableSizeUpdate(stream, _table_connector.GetDynamicTableSize()) == false)
				{
					logte("Failed to encode DynamicTableSizeUpdate (%zu) field", _table_connector.GetDynamicTableSize());
					return nullptr;
				}

				_need_signal_table_size_update = false;
			}

			// First check if the header field is in the table
			auto [name_indexed, value_indexed, index] = _table_connector.LookupIndex(header_fields);

			if (name_indexed == true && value_indexed == true)
			{
				result = EncodeIndexedHeaderField(stream, header_fields, index);
			}
			else
			{
				switch (type)
				{
					case EncodingType::LiteralWithIndexing:
						result = EncodeLiteralHeaderFieldWithIndexing(stream, header_fields, index);
						break;
					case EncodingType::LiteralWithoutIndexing:
						result = EncodeLiteralHeaderFieldWithoutIndexing(stream, header_fields, index);
						break;
					case EncodingType::LiteralNeverIndexed:
						result = EncodeLiteralHeaderFieldNeverIndexed(stream, header_fields, index);
						break;
					default:
						return nullptr;
				}
			}

			if (result == false)
			{
				logte("Failed to encode header field");
				return nullptr;
			}

			return encoded_data;
		}

		bool Encoder::EncodeIndexedHeaderField(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t index)
		{
			return WriteInteger(stream, 0x80, 7, index);
		}

		bool Encoder::EncodeLiteralHeaderFieldWithIndexing(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index)
		{
			if (WriteLiteralHeaderField(stream, header_fields, name_index, 0x40, 6, true) == false)
			{
				return false;
			}

			//TODO(h2) : Check the table size

			_table_connector.Index(header_fields);

			return true;
		}
		
		bool Encoder::EncodeLiteralHeaderFieldWithoutIndexing(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index)
		{
			if (WriteLiteralHeaderField(stream, header_fields, name_index, 0x00, 4, true) == false)
			{
				return false;
			}

			return true;
		}
		
		bool Encoder::EncodeLiteralHeaderFieldNeverIndexed(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index)
		{
			if (WriteLiteralHeaderField(stream, header_fields, name_index, 0x10, 4, true) == false)
			{
				return false;
			}

			return true;
		}

		bool Encoder::EncodeDynamicTableSizeUpdate(ov::ByteStream &stream, size_t size)
		{
			return WriteInteger(stream, 0x20, 5, size);
		}

		bool Encoder::WriteLiteralHeaderField(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index, uint8_t mask, uint8_t index_bits, bool huffman_encoding)
		{
			// https://www.rfc-editor.org/rfc/rfc7541.html#section-6.2

			// If the header field name is in the table
			//   0   1   2   3   4   5   6   7
			// +---+---+---+---+---+---+---+---+
			// |       Mask | Index (n+)       | n : index_bits
			// +---+---+-----------------------+
			// | H |     Value Length (7+)     | H == 1 : Huffman encoded
			// +---+---------------------------+
			// | Value String (Length octets)  |
			// +-------------------------------+

			// // If the header field name is NOT in the table
			//   0   1   2   3   4   5   6   7
			// +---+---+---+---+---+---+---+---+
			// |       Mask | 00000 (n+)       |
			// +---+---+-----------------------+
			// | H |     Name Length (7+)      | 
			// +---+---------------------------+
			// |  Name String (Length octets)  |
			// +---+---------------------------+
			// | H |     Value Length (7+)     |
			// +---+---------------------------+
			// | Value String (Length octets)  |
			// +-------------------------------+

			// Write the header field name
			if (name_index > 0)
			{
				// Write the index
				if (WriteInteger(stream, mask, index_bits, name_index) == false)
				{
					return false;
				}
			}
			else
			{
				uint8_t first_octet = mask; 
				// index == 0

				stream.WriteBE(first_octet);

				// Write the name string
				if (WriteString(stream, header_fields.GetName(), huffman_encoding) == false)
				{
					return false;
				}
			}

			// Write the header field value
			if (WriteString(stream, header_fields.GetValue(), huffman_encoding) == false)
			{
				return false;
			}

			return true;
		}

		bool Encoder::WriteInteger(ov::ByteStream &stream, uint8_t mask, uint8_t value_bits, uint64_t value)
		{
			uint8_t first_octet = mask;
			uint8_t max_prefix_value = std::pow(2, value_bits) - 1;
			
			if (value < max_prefix_value)
			{
				first_octet |= value;
				stream.WriteBE(first_octet);
			}
			else
			{
				first_octet |= max_prefix_value;
				stream.WriteBE(first_octet);

				WriteULEB128(stream, value - max_prefix_value);
			}

			return true;
		}
		
		bool Encoder::WriteULEB128(ov::ByteStream &stream, const uint64_t &value)
		{
			auto temp = value;
			do
			{
				uint8_t octet = temp & 0x7f;
				temp >>= 7;

				if (temp != 0)
				{
					octet |= 0x80;
				}

				stream.WriteBE(octet);

			} while (temp != 0);

			return true;
		}

		bool Encoder::WriteString(ov::ByteStream &stream, const ov::String &value, bool huffman_encoding)
		{
			std::shared_ptr<ov::Data> data = nullptr;

			if (huffman_encoding == true)
			{
				data = HuffmanCodec::GetInstance()->Encode(value);
				if (data == nullptr)
				{
					logte("Failed to encode string");
					return false;
				}
			}
			else
			{
				data = value.ToData(false);
			}

			// Write the length - 0x80 mask means string is Huffman Encoded
			WriteInteger(stream, 0x80, 7, data->GetLength());
			
			// Write encoded string
			stream.Write(data);

			return true;
		}

	} // namespace hpack
} // namespace http