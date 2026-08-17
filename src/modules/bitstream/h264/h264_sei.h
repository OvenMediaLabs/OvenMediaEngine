//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/common_types.h>
#include <base/info/decoder_configuration_record.h>
#include <base/ovlibrary/ovlibrary.h>

#include "h264_parser.h"

// ISO 14496-15, 5.2.4.1
//	aligned(8) class H264SEI {
//      unsigned char(8) 			payloadType
// 		unsigned char(8~variable) 	payloadSize
// 		bit(8*payloadSize) 			payloadData
// 		bit(8) 						rbsp; 			// equal to 0x80(1000 0000'b)
//	}

// aisgned(8) class H264SEIPayloadData
//		unsigned char(128) 	uuid;
//		unsigned int(64) 	timeCode;
// 		bit(8*payloadLengh) payload;
//	}

class H264SEI
{
public:
	enum PayloadType : uint8_t
	{
		BUFFERING_PERIOD = 0,						// Buffering Period
		PICTURE_TIMING = 1,							// Picture Timing
		PAN_SCAN_RECTANGLE = 2,						// Pan-Scan Rectangle
		FILLER_PAYLOAD = 3,							// Filler Payload
		USER_DATA_REGISTERED = 4,					// User Data Registered (ITU-T T.35)
		USER_DATA_UNREGISTERED = 5,					// User Data Unregistered
		RECOVERY_POINT = 6,							// Recovery Point
		REFERENCE_PICTURE_LIST_CONFIRMATION = 7,	// Reference Picture List Confirmation
		DISPLAY_ORIENTATION = 8,					// Display Orientation
		FRAME_PACKING_ARRANGEMENT = 9,				// Frame Packing Arrangement
		PARAMETER_SETS = 10,						// Parameter Sets
		FILM_GRAIN_CHARACTERISTICS = 17,			// Film Grain Characteristics
		CONTENT_LIGHT_LEVEL_INFORMATION = 19,		// Content Light Level Information
		ALTERNATIVE_TRANSFER_CHARACTERISTICS = 45,	// Alternative Transfer Characteristics
		MASTERING_DISPLAY_COLOUR_VOLUME = 47,		// Mastering Display Colour Volume
		UNKNOWN = 255								// Unknown
	};

	static ov::String PayloadTypeToString(PayloadType type)
	{
		switch (type)
		{
			case PayloadType::BUFFERING_PERIOD:
				return "BufferingPeriod";
			case PayloadType::PICTURE_TIMING:
				return "PictureTiming";
			case PayloadType::PAN_SCAN_RECTANGLE:
				return "PanScanRectangle";
			case PayloadType::FILLER_PAYLOAD:
				return "FillerPayload";
			case PayloadType::USER_DATA_REGISTERED:
				return "UserDataRegistered";
			case PayloadType::USER_DATA_UNREGISTERED:
				return "UserDataUnregistered";
			case PayloadType::RECOVERY_POINT:
				return "RecoveryPoint";
			case PayloadType::REFERENCE_PICTURE_LIST_CONFIRMATION:
				return "ReferencePictureListConfirmation";
			case PayloadType::DISPLAY_ORIENTATION:
				return "DisplayOrientation";
			case PayloadType::FRAME_PACKING_ARRANGEMENT:
				return "FramePackingArrangement";
			case PayloadType::PARAMETER_SETS:
				return "ParameterSets";
			case PayloadType::FILM_GRAIN_CHARACTERISTICS:
				return "FilmGrainCharacteristics";
			case PayloadType::CONTENT_LIGHT_LEVEL_INFORMATION:
				return "ContentLightLevelInformation";
			case PayloadType::ALTERNATIVE_TRANSFER_CHARACTERISTICS:
				return "AlternativeTransferCharacteristics";
			case PayloadType::MASTERING_DISPLAY_COLOUR_VOLUME:
				return "MasteringDisplayColourVolume";
			default:
				return "Unknown";
		}
	}

	static PayloadType StringToPayloadType(const ov::String &type)
	{
		if (type == "BufferingPeriod")
			return PayloadType::BUFFERING_PERIOD;
		else if (type == "PictureTiming")
			return PayloadType::PICTURE_TIMING;
		else if (type == "PanScanRectangle")
			return PayloadType::PAN_SCAN_RECTANGLE;
		else if (type == "FillerPayload")
			return PayloadType::FILLER_PAYLOAD;
		else if (type == "UserDataRegistered")
			return PayloadType::USER_DATA_REGISTERED;
		else if (type == "UserDataUnregistered")
			return PayloadType::USER_DATA_UNREGISTERED;
		else if (type == "RecoveryPoint")
			return PayloadType::RECOVERY_POINT;
		else if (type == "ReferencePictureListConfirmation")
			return PayloadType::REFERENCE_PICTURE_LIST_CONFIRMATION;
		else if (type == "DisplayOrientation")
			return PayloadType::DISPLAY_ORIENTATION;
		else if (type == "FramePackingArrangement")
			return PayloadType::FRAME_PACKING_ARRANGEMENT;
		else if (type == "ParameterSets")
			return PayloadType::PARAMETER_SETS;
		else if (type == "FilmGrainCharacteristics")
			return PayloadType::FILM_GRAIN_CHARACTERISTICS;
		else if (type == "ContentLightLevelInformation")
			return PayloadType::CONTENT_LIGHT_LEVEL_INFORMATION;
		else if (type == "AlternativeTransferCharacteristics")
			return PayloadType::ALTERNATIVE_TRANSFER_CHARACTERISTICS;
		else if (type == "MasteringDisplayColourVolume")
			return PayloadType::MASTERING_DISPLAY_COLOUR_VOLUME;

		return PayloadType::UNKNOWN;
	}

	// OME Specific UUID
	// 464d4c47-5241-494e-434f-4c4f-55524201
	static constexpr uint8_t OME_PAYLOAD_DATA_TYPE1_UUID[16] = {0x46, 0x4D, 0x4C, 0x47, 0x52, 0x41, 0x49, 0x4E, 0x43, 0x4F, 0x4C, 0x4F, 0x55, 0x52, 0x42, 0x01};

	enum PayloadDataType : uint8_t
	{
		RAW = 0,
		TYPE1 = 1
	};

	ov::String PayloadDataTypeToString(PayloadDataType type)
	{
		switch (type)
		{
			case PayloadDataType::RAW:
				return "Raw";
			case PayloadDataType::TYPE1:
				return "Type1";
			default:
				return "Unknown";
		}
	}

public:
	H264SEI() = default;
	~H264SEI() = default;

	std::shared_ptr<ov::Data> Serialize();
	static std::shared_ptr<H264SEI> Parse(const std::shared_ptr<const ov::Data> &data);

	// Set payload type
	void SetPayloadType(PayloadType payload_type)
	{
		_payload_type = payload_type;
	}

	PayloadType GetPayloadType() const
	{
		return _payload_type;
	}

	void SetPayloadDataType(PayloadDataType payload_data_type)
	{
		_payload_data_type = payload_data_type;
	}

	PayloadDataType GetPayloadDataType() const
	{
		return _payload_data_type;
	}

	// Epoch time in milliseconds
	void SetPayloadTimeCode(uint64_t payload_time_code)
	{
		_payload_time_code = payload_time_code;
	}

	uint64_t GetPayloadTimeCode() const
	{
		return _payload_time_code;
	}

	// Set payload data (plintext or binary)
	void SetPayloadData(const std::shared_ptr<ov::Data> &payload_data)
	{
		_payload_data = payload_data;
	}

	std::shared_ptr<ov::Data> GetPayloadData() const
	{
		return _payload_data;
	}

	static std::shared_ptr<ov::Data> HexStrToByteArray(const std::shared_ptr<ov::Data> &hex_string);

	ov::String GetInfoString();

private:
	PayloadType _payload_type = PayloadType::USER_DATA_UNREGISTERED;
	PayloadDataType _payload_data_type = PayloadDataType::TYPE1;
	uint64_t _payload_time_code = 0;
	std::shared_ptr<ov::Data> _payload_data = nullptr;
	uint8_t _rbsp_trailing_bits = 0x80;
};