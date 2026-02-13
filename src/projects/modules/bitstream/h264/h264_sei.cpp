//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "h264_sei.h"

#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/bit_writer.h>
#include <base/ovlibrary/hex.h>
#include <base/ovlibrary/ovlibrary.h>

#define OV_LOG_TAG "H264SEI"

std::shared_ptr<ov::Data> H264SEI::Serialize()
{
	// ------------------------------
	// SEI Payload Data
	// ------------------------------
	ov::BitWriter sei_payload_data(1024);

	// Payload Data : UUID + (TimeCode)
	switch (_payload_data_type)
	{
		case PayloadDataType::RAW:
			break;
		case PayloadDataType::TYPE1: {
			// Write UUID
			sei_payload_data.WriteData(OME_PAYLOAD_DATA_TYPE1_UUID, sizeof(OME_PAYLOAD_DATA_TYPE1_UUID));

			// Write TimeCode
			sei_payload_data.WriteBytes<uint64_t>(_payload_time_code);
			break;
		}
	}

	if (_payload_data != nullptr)
	{
		// Write Data
		sei_payload_data.WriteData(_payload_data->GetDataAs<uint8_t>(), _payload_data->GetLength());
	}

	// ------------------------------
	// SEI Payload
	// ------------------------------

	// Calculate Payload Size
	auto payload_size = sei_payload_data.GetDataSize();

	ov::BitWriter writer(payload_size + 3);	 // payload data + payload_size(max 2)  + rbsp

	// Payload Type (8)
	writer.WriteBytes<uint8_t>(_payload_type);

	// Payload Size
	auto remaining_size = payload_size;
	while (remaining_size >= 255)
	{
		writer.WriteBytes<uint8_t>(255);
		remaining_size -= 255;
	}
	writer.WriteBytes<uint8_t>(remaining_size);

	// Payload Data
	writer.WriteData(sei_payload_data.GetData(), sei_payload_data.GetDataSize());

	// RBSP_trailing_bits
	writer.WriteBits(8, _rbsp_trailing_bits);

	return writer.GetDataObject();
}

std::shared_ptr<H264SEI> H264SEI::Parse(const std::shared_ptr<const ov::Data> &data)
{
	auto sei = std::make_shared<H264SEI>();

	BitReader reader(data);

	// Payload Type
	auto payload_type = static_cast<PayloadType>(reader.ReadBytes<uint8_t>());
	sei->SetPayloadType(payload_type);

	if (payload_type != PayloadType::USER_DATA_UNREGISTERED)
	{
		loge("H264SEI", "Unsupported SEI Payload Type: %s (%u)", PayloadTypeToString(payload_type).CStr(), static_cast<uint8_t>(payload_type));
		return nullptr;
	}

	// Payload Size
	size_t payload_size = 0;
	while (true)
	{
		auto size = reader.ReadBytes<uint8_t>();
		payload_size += size;
		if (size < 255)
			break;
	}

	// Payload Data
	auto payload_data = std::make_shared<ov::Data>(reader.CurrentPosition(), reader.BytesRemained());

	if (payload_data->GetLength() < payload_size)
	{
		loge("H264SEI", "Invalid SEI Payload Data Size: %zu", payload_data->GetLength());
		return nullptr;
	}

	if (payload_data->GetLength() < sizeof(OME_PAYLOAD_DATA_TYPE1_UUID) + sizeof(uint64_t))
	{
		loge("H264SEI", "Invalid SEI Payload Data Size: %zu", payload_data->GetLength());
		return nullptr;
	}

	BitReader payload_reader(payload_data);

	// Read UUID
	uint8_t uuid[16] = {0};
	for (size_t i = 0; i < sizeof(uuid); i++)
	{
		uuid[i] = payload_reader.ReadBytes<uint8_t>();
	}

	if (memcmp(uuid, OME_PAYLOAD_DATA_TYPE1_UUID, sizeof(OME_PAYLOAD_DATA_TYPE1_UUID)) != 0)
	{
		loge("H264SEI", "Unsupported SEI Payload Data Type");
		return nullptr;
	}

	// Parse OME_PAYLOAD_DATA_TYPE1_UUID Payload data type only.
	// TODO: If other data types are added in the future, the code below should be improved.

	// Read TimeCode
	uint64_t time_code = payload_reader.ReadBytes<uint64_t>();
	sei->SetPayloadTimeCode(time_code);

	// Read Payload Data
	auto payload_data_size = payload_size - sizeof(OME_PAYLOAD_DATA_TYPE1_UUID) - sizeof(uint64_t);
	if (payload_data_size > 0)
	{
		auto payload_data = std::make_shared<ov::Data>(payload_reader.CurrentPosition(), payload_data_size);
		sei->SetPayloadData(payload_data);
	}

	return sei;
}

ov::String H264SEI::GetInfoString()
{
	ov::String info;

	info.AppendFormat("Payload Type: %s (%u, ", PayloadTypeToString(_payload_type).CStr(), static_cast<uint8_t>(_payload_type));
	info.AppendFormat("Payload Data (Type): %s, ", PayloadDataTypeToString(_payload_data_type).CStr());
	info.AppendFormat("Payload Data (Timecode): %llu, ", _payload_time_code);
	if (_payload_data != nullptr)
	{
		info.AppendFormat("Payload Data (Data): %d bytes\n%s", _payload_data->GetLength(), _payload_data->Dump(nullptr, nullptr).CStr());
	}

	return info;
}