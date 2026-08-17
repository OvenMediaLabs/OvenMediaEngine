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

#include <unordered_set>

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

			// Headers listed here carry a value that is unique per response, so indexing
			// them never gets a table hit and only churns the dynamic table
			static EncodingType GetEncodingTypeOf(const ov::String &header_name)
			{
				static const std::unordered_set<ov::String, ov::CaseInsensitiveHash, ov::CaseInsensitiveEqual> without_indexing_headers = {
					"etag",
				};

				return (without_indexing_headers.find(header_name) != without_indexing_headers.end())
						   ? EncodingType::LiteralWithoutIndexing
						   : EncodingType::LiteralWithIndexing;
			}

			// The peer replays the table updates in the order the header blocks arrive on
			// the wire, so encoding a block and sending it must be one step. Streams share
			// this encoder, so the caller holds this from the first Encode() of a header
			// block until its frames are sent.
			ov::Mutex &GetHeaderBlockLock()
			{
				return _header_block_lock;
			}

		private:
			bool EncodeIndexedHeaderField(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t index) OV_REQUIRES(_encoder_lock);
			bool EncodeLiteralHeaderFieldWithIndexing(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index) OV_REQUIRES(_encoder_lock);
			bool EncodeLiteralHeaderFieldWithoutIndexing(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index) OV_REQUIRES(_encoder_lock);
			bool EncodeLiteralHeaderFieldNeverIndexed(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index) OV_REQUIRES(_encoder_lock);

			bool EncodeDynamicTableSizeUpdate(ov::ByteStream &stream, size_t size) OV_REQUIRES(_encoder_lock);

			bool WriteLiteralHeaderField(ov::ByteStream &stream, const HeaderField &header_fields, uint32_t name_index, uint8_t mask, uint8_t index_bits, bool huffman_encoding = true) OV_REQUIRES(_encoder_lock);
			bool WriteInteger(ov::ByteStream &stream, uint8_t mask, uint8_t value_bits, uint64_t value) OV_REQUIRES(_encoder_lock);
			bool WriteString(ov::ByteStream &stream, const ov::String &value, bool huffman_encoding) OV_REQUIRES(_encoder_lock);

			// Unsigned Little Endian Base 128
			bool WriteULEB128(ov::ByteStream &stream, const uint64_t &value) OV_REQUIRES(_encoder_lock);

			TableConnector	_table_connector OV_GUARDED_BY(_encoder_lock);
			bool _need_signal_table_size_update OV_GUARDED_BY(_encoder_lock) = false;

			ov::Mutex _encoder_lock;

			// Always taken before _encoder_lock
			ov::Mutex _header_block_lock;
		};
	} // namespace hpack
} // namespace http