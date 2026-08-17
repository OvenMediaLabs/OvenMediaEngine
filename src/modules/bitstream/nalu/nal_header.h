#pragma once

#include <base/common_types.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/h264/h264_common.h>

class NalHeader
{
public:
	struct H264NALHeader
	{
		uint8_t forbidden_zero_bit : 1;	 // Must be 0
		uint8_t nal_ref_idc : 2;		 // Importance (0~3)
		uint8_t nal_unit_type : 5;		 // NAL Type (1~31)

		// Convert struct to a single byte
		uint8_t toByte() const
		{
			return (forbidden_zero_bit << 7) | (nal_ref_idc << 5) | (nal_unit_type);
		}

		// Create H.264 NAL Header from a byte
		static H264NALHeader fromByte(uint8_t byte)
		{
			return {
				static_cast<uint8_t>((byte >> 7) & 0x01),
				static_cast<uint8_t>((byte >> 5) & 0x03),
				static_cast<uint8_t>(byte & 0x1F)};
		}
	};

	struct H265NALHeader
	{
		uint8_t forbidden_zero_bit : 1;		// Must be 0
		uint8_t nal_unit_type : 6;			// NAL Type (0~63)
		uint8_t nuh_layer_id : 6;			// Layer ID (0~63, usually 0)
		uint8_t nuh_temporal_id_plus1 : 3;	// Temporal ID + 1 (1~7)

		// Convert struct to two bytes
		uint16_t toBytes() const
		{
			return (forbidden_zero_bit << 15) | (nal_unit_type << 9) | (nuh_layer_id << 3) | (nuh_temporal_id_plus1);
		}

		// Create H.265 NAL Header from two bytes
		static H265NALHeader fromBytes(uint16_t bytes)
		{
			return {
				static_cast<uint8_t>((bytes >> 15) & 0x01),
				static_cast<uint8_t>((bytes >> 9) & 0x3F),
				static_cast<uint8_t>((bytes >> 3) & 0x3F),
				static_cast<uint8_t>(bytes & 0x07)};
		}
	};

	static std::shared_ptr<ov::Data> CreateH264(uint8_t nal_ref_idc, uint8_t nal_unit_type)
	{
		H264NALHeader h264_nal = {0, nal_ref_idc, nal_unit_type};

		ov::BitWriter writer(8);
		writer.WriteBytes<uint8_t>(h264_nal.toByte());

		return writer.GetDataObject();
	}

	static std::shared_ptr<ov::Data> CreateH264(uint8_t nal_unit_type)
	{
		return CreateH264(0, nal_unit_type);
	}

	static std::shared_ptr<ov::Data> CreateH265(uint8_t nal_unit_type, uint8_t nuh_layer_id, uint8_t nuh_temporal_id_plus1)
	{
		H265NALHeader h265_nal = {0, nal_unit_type, nuh_layer_id, nuh_temporal_id_plus1};

		ov::BitWriter writer(16);
		writer.WriteBytes<uint16_t>(h265_nal.toBytes());

		return writer.GetDataObject();
	}
};
