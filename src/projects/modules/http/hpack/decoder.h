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
		class Decoder
		{
		public:
			bool Decode(const std::shared_ptr<const ov::Data> &data, std::vector<HeaderField> &header_fields);

		private:
			bool DecodeIndexedHeaderField(const std::shared_ptr<BitReader> &reader, std::vector<HeaderField> &header_fields);
			bool DecodeLiteralHeaderFieldWithIndexing(const std::shared_ptr<BitReader> &reader, std::vector<HeaderField> &header_fields);
			bool DecodeLiteralHeaderFieldWithoutIndexing(const std::shared_ptr<BitReader> &reader, std::vector<HeaderField> &header_fields);
			bool DecodeLiteralHeaderFieldNeverIndexed(const std::shared_ptr<BitReader> &reader, std::vector<HeaderField> &header_fields);

			bool DecodeDynamicTableSizeUpdate(const std::shared_ptr<BitReader> &reader);

			bool ReadLiteralHeaderField(uint8_t index_bits, const std::shared_ptr<BitReader> &reader, HeaderField &header_field);

			// Unsigned Little Endian Base 128
			bool ReadULEB128(const std::shared_ptr<BitReader> &reader, uint64_t &value);
			bool ReadString(const std::shared_ptr<BitReader> &reader, ov::String &value);

			TableConnector	_table_connector;
			std::mutex _decoder_lock;
		};
	} // namespace hpack
} // namespace http