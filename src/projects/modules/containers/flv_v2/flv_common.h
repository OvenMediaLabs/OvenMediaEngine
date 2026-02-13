//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/decoder_configuration_record.h>
#include <base/ovlibrary/ovlibrary.h>

#include "./flv_datastructure.h"

namespace modules
{
	namespace flv
	{
		struct CommonData
		{
			CommonData(uint32_t default_track_id, bool from_ex_header)
				: from_ex_header(from_ex_header),
				  track_id(default_track_id)
			{
			}

			virtual ~CommonData() = default;

			bool from_ex_header;
			uint32_t track_id;

			std::shared_ptr<DecoderConfigurationRecord> header;

			// This is used to store the data to re-parse `DecoderConfigurationRecord`
			// when receiving `cmn::PacketType::SEQUENCE_HEADER`
			// in `MediaRouterNormalize::Process*()`.
			// It can be improved to use what is parsed here later.
			std::shared_ptr<const ov::Data> header_data;

			std::shared_ptr<const ov::Data> payload = nullptr;
		};

		class ParserCommon
		{
		public:
			ParserCommon(int default_track_id)
				: _default_track_id(default_track_id)
			{
			}

			virtual bool Parse(ov::BitReader &reader) = 0;

			OV_DEFINE_CONST_GETTER(IsExHeader, _is_ex_header, noexcept);

			OV_DEFINE_CONST_GETTER(IsMultitrack, _is_multitrack, noexcept);
			OV_DEFINE_CONST_GETTER(GetMultitrackType, _multitrack_type, noexcept);

		protected:
			std::shared_ptr<const ov::Data> GetPayload(bool is_multi_track, ov::BitReader &reader, uint24_t size_of_track, size_t size_of_track_offset)
			{
				static auto EMPTY_DATA = std::make_shared<ov::Data>();

				size_t payload_size	   = 0;

				if ((is_multi_track == false) || (size_of_track == 0))
				{
					payload_size = reader.GetRemainingBytes();
				}
				else
				{
					// How many bytes have been read since the sizeOfVideoTrack field
					auto read_bytes_since_size_of_track = reader.GetByteOffset() - size_of_track_offset;
					payload_size						= size_of_track - read_bytes_since_size_of_track;
				}

				return (payload_size > 0) ? reader.ReadBytes(payload_size) : EMPTY_DATA;
			}

		protected:
			uint32_t _default_track_id;

			bool _is_ex_header				  = false;

			bool _is_multitrack				  = false;
			AvMultitrackType _multitrack_type = AvMultitrackType::OneTrack;
		};
	}  // namespace flv
}  // namespace modules
