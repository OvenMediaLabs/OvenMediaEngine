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

#include <algorithm>

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
			// Returns the number of bytes that belong to the current track at the reader's
			// position. In multitrack mode this is bounded by `sizeOfVideoTrack` so that a
			// reader holding several concatenated tracks does not spill into the next track.
			size_t GetRemainingTrackSize(bool is_multi_track, const ov::BitReader &reader, uint24_t size_of_track, size_t size_of_track_offset)
			{
				const auto remaining_bytes = reader.GetRemainingBytes();

				if ((is_multi_track == false) || (size_of_track == 0))
				{
					return remaining_bytes;
				}

				// How many bytes have been read since the sizeOfVideoTrack field
				auto read_bytes_since_size_of_track = reader.GetByteOffset() - size_of_track_offset;

				// A malformed `sizeOfVideoTrack` may be smaller than what has already been read.
				// Treat that as an empty track rather than underflowing to a huge `size_t`.
				if (size_of_track <= read_bytes_since_size_of_track)
				{
					return 0;
				}

				// Never report more than what is actually left in the buffer.
				return std::min(static_cast<size_t>(size_of_track) - read_bytes_since_size_of_track, remaining_bytes);
			}

			std::shared_ptr<const ov::Data> GetPayload(bool is_multi_track, ov::BitReader &reader, uint24_t size_of_track, size_t size_of_track_offset)
			{
				static auto EMPTY_DATA = std::make_shared<ov::Data>();

				size_t payload_size	   = GetRemainingTrackSize(is_multi_track, reader, size_of_track, size_of_track_offset);

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
