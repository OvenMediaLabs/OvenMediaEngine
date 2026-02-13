//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "data_structure.h"
#include "table_connector.h"

namespace http
{
	// https://www.rfc-editor.org/rfc/rfc7541.html
	namespace hpack
	{
		class Encoder
		{
		public:
			enum class EncodingType
			{
				LiteralWithIndexing,
				LiteralWithoutIndexing,
				LiteralNeverIndexed
			};

			bool UpdateDynamicTableSize(size_t size);

			std::shared_ptr<ov::Data> Encode(const HeaderField &header_fields, EncodingType type);

		private:
			bool EncodeIndexedHeaderField(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t index);
			bool EncodeLiteralHeaderFieldWithIndexing(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index);
			bool EncodeLiteralHeaderFieldWithoutIndexing(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index);
			bool EncodeLiteralHeaderFieldNeverIndexed(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index);

			bool EncodeDynamicTableSizeUpdate(ov::ByteStream &stream, size_t size);
			
			bool WriteLiteralHeaderField(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index, uint8_t mask, uint8_t index_bits, bool huffman_encoding = true);
			bool WriteInteger(ov::ByteStream &stream, uint8_t mask, uint8_t value_bits, uint64_t value);
			bool WriteString(ov::ByteStream &stream, const ov::String &value, bool huffman_encoding);

			// Unsigned Little Endian Base 128
			bool WriteULEB128(ov::ByteStream &stream, const uint64_t &value);

			TableConnector	_table_connector;
			bool _need_signal_table_size_update = false;

			std::mutex _encoder_lock;
		};
	} // namespace hpack
} // namespace http