//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "rtp_depacketizer_h265.h"

#include <base/ovlibrary/byte_io.h>

std::shared_ptr<ov::Data> RtpDepacketizerH265::ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list)
{
	if (payload_list.size() <= 0)
	{
		return nullptr;
	}

	auto reserve_size = 0;
	for (auto &payload : payload_list)
	{
		reserve_size += payload->GetLength();
		reserve_size += 16;	 // spare
	}

	auto bitstream = std::make_shared<ov::Data>(reserve_size);
	for (const auto &payload : payload_list)
	{
		if (payload->GetLength() < PAYLOAD_HEADER_SIZE)
		{
			continue;
		}

		/*
		The structure of HEVC NAL unit header (payload header)
		+---------------+---------------+
		|0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		|F|   Type    |  LayerId  | TID |
		+-------------+-----------------+
		*/
		uint16_t payload_hdr = ByteReader<uint16_t>::ReadBigEndian(&(payload->GetDataAs<uint8_t>()[0]));
		uint8_t nuh_type = NUH_TYPE(payload_hdr);
#if 0
		uint8_t nuh_forbidden_zero = NUH_FORBIDDEN(payload_hdr);
		uint8_t nuh_layer_id = NUH_LAYER_ID(payload_hdr);
		uint8_t nuh_temporal_id = NUH_TEMPORAL_ID(payload_hdr);

		logw("RtpDepacketizerH265", "forbidden_zero = %d, type = %d, layer_id = %d, temporal_id = %d, length = %d", 
			nuh_forbidden_zero, nuh_type, nuh_layer_id, nuh_temporal_id, payload->GetLength());
#endif

		std::shared_ptr<ov::Data> result;

		// Fragmentation Units (FUs)
		if (nuh_type == H265NaluType::kFUs)
		{
			result = ParseFUsAndConvertAnnexB(payload);
			if (result == nullptr)
			{
				loge("RtpDepacketizerH265", "Failed to parse FU");
				return nullptr;
			}

			bitstream->Append(result);
		}
		// Aggregation Packets (APs)
		else if (nuh_type == H265NaluType::kAPs)
		{
			result = ParseAPsAndConvertAnnexB(payload);
			if (result == nullptr)
			{
				loge("RtpDepacketizerH265", "Failed to parse AP");
				return nullptr;
			}

			bitstream->Append(result);
		}
		else if (nuh_type == H265NaluType::kPACI)
		{
			logw("RtpDepacketizerH265", "PACI is not supported yet");
			return nullptr;
		}
		// Single NAL Unit Packets
		else
		{
			result = ConvertSingleNaluToAnnexB(payload);
			if (result == nullptr)
			{
				loge("RtpDepacketizerH265", "Failed to convert single NALU");
				return nullptr;
			}

			bitstream->Append(result);
		}
	}

	return bitstream;
}

std::shared_ptr<ov::Data> RtpDepacketizerH265::ParseFUsAndConvertAnnexB(const std::shared_ptr<ov::Data> &payload)
{
	/*
	The structure of an FU
     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |    PayloadHdr (Type=49)       |   FU header   | DONL (cond)   |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-| 
    | DONL (cond)   |                                               |
    |-+-+-+-+-+-+-+-+                                               |
    |                         FU payload                            |
    |                                                               |
    |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                               :...OPTIONAL RTP padding        |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+	

    The Structure of FU Header
    +---------------+
    |0|1|2|3|4|5|6|7|
    +-+-+-+-+-+-+-+-+
    |S|E|  FuType   |
    +---------------+
	*/

	auto bitstream = std::make_shared<ov::Data>(payload->GetLength() + 16);
	uint8_t start_prefix[ANNEXB_START_PREFIX_LENGTH] = {0, 0, 0, 1};

	// Check Payload header length and FU header length
	if (payload->GetLength() < PAYLOAD_HEADER_SIZE + FU_HEADER_SIZE)
	{
		// Invalid Data
		return nullptr;
	}

	auto buffer = payload->GetDataAs<uint8_t>();
	size_t offset = 0;

	// Payload Header
	uint16_t payload_hdr = ByteReader<uint16_t>::ReadBigEndian(&buffer[offset]);
	[[maybe_unused]] uint8_t nuh_forbidden_zero = NUH_FORBIDDEN(payload_hdr);
	[[maybe_unused]] uint8_t nuh_type = NUH_TYPE(payload_hdr);
	uint8_t nuh_layer_id = NUH_LAYER_ID(payload_hdr);
	uint8_t nuh_temporal_id = NUH_TEMPORAL_ID(payload_hdr);
	offset += PAYLOAD_HEADER_SIZE;

	// FU header
	uint8_t fu_hdr = buffer[offset];
	uint8_t fu_s = FUH_SBIT(fu_hdr);
	[[maybe_unused]] uint8_t fu_e = FUH_EBIT(fu_hdr);
	uint8_t fu_type = FUH_TYPE(fu_hdr);
	offset += FU_HEADER_SIZE;

	// S, E fields cannot both be 1.
	if (fu_s == 1 && fu_e == 1)
	{
		// Invalid Data
		return nullptr;
	}

	// If the S field is 1, the FU header must include the DONL(Conditional) field.
	if (fu_s == 1 && payload->GetLength() < PAYLOAD_HEADER_SIZE + FU_HEADER_SIZE + DONL_FIELD_SIZE)
	{
		// Invalid Data
		return nullptr;
	}

	// If the S field is 1, the NAL header must be attached.
	if (fu_s == 1)
	{
		// logw("RtpDepacketizerH265", "start=%d, end=%d, type=%d->%d, layer=%d, temporal_id=%d", fu_s, fu_e, nuh_type, fu_type, nuh_layer_id, nuh_temporal_id);
		bitstream->Append(start_prefix, ANNEXB_START_PREFIX_LENGTH);

		// Set NAL Header
		uint8_t nal_header[2];
		ByteWriter<uint16_t>::WriteBigEndian((uint8_t *)&nal_header[0], NUH_HEADER(fu_type, nuh_layer_id, nuh_temporal_id));
		bitstream->Append(nal_header, PAYLOAD_HEADER_SIZE);
	}

	// Set FU Payload
	bitstream->Append(&buffer[offset], payload->GetLength() - PAYLOAD_HEADER_SIZE - FU_HEADER_SIZE);

	return bitstream;
}

std::shared_ptr<ov::Data> RtpDepacketizerH265::ParseAPsAndConvertAnnexB(const std::shared_ptr<ov::Data> &payload)
{
	/*
	AP packet containing two aggregation units without the DONL and DOND fields
	 0                   1                   2                   3
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                          RTP Header                           |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|   PayloadHdr (Type=48)        |         NALU 1 Size           |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|          NALU 1 HDR           |                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+         NALU 1 Data           |
	|                   . . .                                       |
	|                                                               |
	+               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|  . . .        | NALU 2 Size                   | NALU 2 HDR    |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	| NALU 2 HDR    |                                               |
	+-+-+-+-+-+-+-+-+              NALU 2 Data                      |
	|                   . . .                                       |
	|                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                               :...OPTIONAL RTP padding        |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

	AP containing two aggregation units with the DONL and DOND fields
	 0                   1                   2                   3
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                          RTP Header                           |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|   PayloadHdr (Type=48)        |        NALU 1 DONL            |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|          NALU 1 Size          |            NALU 1 HDR         |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                                                               |
	|                 NALU 1 Data   . . .                           |
	|                                                               |
	+     . . .     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|               |  NALU 2 DOND  |          NALU 2 Size          |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|          NALU 2 HDR           |                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+          NALU 2 Data          |
	|                                                               |
	|        . . .                  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                               :...OPTIONAL RTP padding        |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	*/
	auto bitstream = std::make_shared<ov::Data>(payload->GetLength() + 1024);

	if (payload->GetLength() < PAYLOAD_HEADER_SIZE + LENGTH_FIELD_SIZE)
	{
		return nullptr;
	}

	auto buffer = payload->GetDataAs<uint8_t>();
	size_t payload_length = payload->GetLength();
	size_t offset = 0;

	// Skip Payload Header
	offset += PAYLOAD_HEADER_SIZE;	// PAYLOAD HDR LENGTH

	while (offset < payload_length)
	{
		// if(donl) {
		// 	offset += DONL_FIELD_SIZE;
		// }
		// else if(dond) {
		// 	offset += DOND_FIELD_SIZE;
		// }

		// Get NAL Length
		uint16_t nalu_size = ByteReader<uint16_t>::ReadBigEndian(&buffer[offset]);
		offset += LENGTH_FIELD_SIZE;
		if (offset + nalu_size > payload_length)
		{
			return nullptr;
		}

		uint16_t nalu_hdr = ByteReader<uint16_t>::ReadBigEndian(&buffer[offset]);
		uint8_t nuh_type = NUH_TYPE(nalu_hdr);
		uint8_t nuh_layer_id = NUH_LAYER_ID(nalu_hdr);
		uint8_t nuh_temporal_id = NUH_TEMPORAL_ID(nalu_hdr);
		offset += PAYLOAD_HEADER_SIZE;

		// logw("RtpDepacketizerH265", "type=%d, layer=%d, temporal_id=%d", nuh_type, nuh_layer_id, nuh_temporal_id);

		// NAL Header
		uint8_t nal_header[2];
		ByteWriter<uint16_t>::WriteBigEndian((uint8_t *)&nal_header[0], NUH_HEADER(nuh_type, nuh_layer_id, nuh_temporal_id));
		nalu_size -= PAYLOAD_HEADER_SIZE;

		AppendBitstream(bitstream, nuh_type, nal_header, &buffer[offset], nalu_size);

		offset += nalu_size;
	}

	return bitstream;
}

std::shared_ptr<ov::Data> RtpDepacketizerH265::ConvertSingleNaluToAnnexB(const std::shared_ptr<ov::Data> &payload)
{
	/*
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           PayloadHdr          |      DONL (conditional)       |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                                                               |
   |                  NAL unit payload data                        |
   |                                                               |
   |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                               :...OPTIONAL RTP padding        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	*/
	auto bitstream = std::make_shared<ov::Data>(payload->GetLength() + ANNEXB_START_PREFIX_LENGTH);
	uint16_t nalu_size = payload->GetLength();
	size_t offset = 0;

	// Payload Header
	auto buffer = payload->GetDataAs<uint8_t>();

	uint16_t payload_hdr = ByteReader<uint16_t>::ReadBigEndian(&buffer[offset]);
	uint8_t nuh_forbidden_zero = NUH_FORBIDDEN(payload_hdr);
	uint8_t nuh_type = NUH_TYPE(payload_hdr);
	uint8_t nuh_layer_id = NUH_LAYER_ID(payload_hdr);
	uint8_t nuh_temporal_id = NUH_TEMPORAL_ID(payload_hdr);
	offset += PAYLOAD_HEADER_SIZE;

	// logw("RtpDepacketizerH265", "type=%d, layer=%d, temporal_id=%d", nuh_type, nuh_layer_id, nuh_temporal_id);

	if (nuh_forbidden_zero != 0)
	{
		// Invalid Data
		return nullptr;
	}
	
	// NAL Header
	uint8_t nal_header[2];
	ByteWriter<uint16_t>::WriteBigEndian((uint8_t *)&nal_header[0], NUH_HEADER(nuh_type, nuh_layer_id, nuh_temporal_id));
	nalu_size -= PAYLOAD_HEADER_SIZE;

	AppendBitstream(bitstream, nuh_type, nal_header, &buffer[offset], nalu_size);

	return bitstream;
}

void RtpDepacketizerH265::AppendBitstream(std::shared_ptr<ov::Data> &bitstream, uint8_t nal_type, uint8_t nal_header[2], const void *payload, size_t payload_length)
{
	uint8_t start_prefix[ANNEXB_START_PREFIX_LENGTH] = {0, 0, 0, 1};
	
	// Append Start Prefix
	bitstream->Append(start_prefix, ANNEXB_START_PREFIX_LENGTH);

	// Append NAL Header
	bitstream->Append(nal_header, PAYLOAD_HEADER_SIZE);

	// Append NAL Data
	bitstream->Append(payload, payload_length);

	if (IsDecodingParmeterSets(nal_type) == true)
	{
		auto data = std::make_shared<ov::Data>();
		data->Append(nal_header, PAYLOAD_HEADER_SIZE);
		data->Append(payload, payload_length);

		AddDecodingParameterSet(nal_type, data);

		// logt("RtpDepacketizerH265", "Decoding Parameter Sets : %d, Map.size : %d", nal_type, GetDecodingParameterSets().size());
	}
}

bool RtpDepacketizerH265::IsDecodingParmeterSets(uint8_t nal_unit_type)
{
	return (nal_unit_type == H265NaluType::kVPS ||
			nal_unit_type == H265NaluType::kSPS ||
			nal_unit_type == H265NaluType::kPPS);
}

std::shared_ptr<ov::Data> RtpDepacketizerH265::GetDecodingParameterSetsToAnnexB()
{
	uint8_t start_prefix[ANNEXB_START_PREFIX_LENGTH] = {0, 0, 0, 1};

	if(GetDecodingParameterSets().size() < 3) // It must contatin VPS, SPS, PPS
	{
		return nullptr;
	}

	auto data = std::make_shared<ov::Data>();
	for (auto &it : GetDecodingParameterSets())
	{
		data->Append(start_prefix, ANNEXB_START_PREFIX_LENGTH);
		data->Append(it.second->GetData(), it.second->GetLength());
	}

	return data;
}