//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/ovlibrary.h>

// ISO 14496-15, 5.2.4.1
//	aligned(8) class H264SEI {
//      unsigned char(8) 			payloadType
// 		unsigned char(8~variable) 	payloadSize
// 		bit(8*payloadSize) 			payloadData
// 		bit(8) 						rbsp; 			// equal to 0x80(1000 0000'b)
//	}

// Model of pic_timing( ), ITU-T H.264 D.1.2

// One clock_timestamp[i] entry inside pic_timing.
struct H264SeiClockTimestamp
{
	// clock_timestamp_flag. False is a slot that carries no timestamp; it still holds its place in
	// H264SeiPicTiming::clock_timestamps, which is indexed by slot number.
	bool present = true;
	// 0 = progressive, 1 = interlaced, 2 = unknown, 3 = reserved
	uint8_t ct_type = 0;
	// nuit_field_based_flag
	bool units_field_based_flag = false;
	// SMPTE counting method. 1 = non-drop frame, 2 = drop frame
	uint8_t counting_type = 1;
	// True: hours/minutes/seconds are all written. False: only the changed fields.
	bool full_timestamp_flag = true;
	// Set on the first stamped picture and on any break in continuity
	bool discontinuity_flag = false;
	// Only meaningful when counting_type is 2 (drop frame)
	bool cnt_dropped_flag = false;
	// The wire field is u(8); a value above 255 is truncated on write
	uint16_t n_frames = 0;
	uint8_t hours	  = 0;	// 0~23
	uint8_t minutes	  = 0;	// 0~59
	uint8_t seconds	  = 0;	// 0~59
	int32_t time_offset = 0;
	// Read from the SPS hrd_parameters()
	uint8_t time_offset_length = 24;

	ov::String GetTimecodeString() const
	{
		return ov::String::FormatString("%02u:%02u:%02u:%02u",
										hours, minutes, seconds, n_frames);
	}
};

// pic_timing (payloadType 1)
struct H264SeiPicTiming
{
	// Present only when CpbDpbDelaysPresentFlag is 1; bit widths come from the SPS. Carried over
	// verbatim when an existing pic_timing is rewritten.
	uint32_t cpb_removal_delay = 0;
	uint32_t dpb_output_delay  = 0;

	// Present only when pic_struct_present_flag is 1. 0 = progressive frame (NumClockTS = 1).
	uint8_t pic_struct = 0;

	// Indexed by slot number, NumClockTS of them (H.264 Table D-1). A slot with `present` false,
	// or one past the end of the vector, carries no timestamp.
	std::vector<H264SeiClockTimestamp> clock_timestamps;

	// ITU-T H.264 Table D-1
	static uint8_t NumClockTSFromPicStruct(uint8_t pic_struct)
	{
		switch (pic_struct)
		{
			case 0:	 // progressive frame
			case 1:	 // top field
			case 2:	 // bottom field
				return 1;
			case 3:	 // top field, bottom field
			case 4:	 // bottom field, top field
			case 7:	 // frame doubling
				return 2;
			case 5:	 // top field, bottom field, top field repeated
			case 6:	 // bottom field, top field, bottom field repeated
			case 8:	 // frame tripling
				return 3;
			default:
				return 0;
		}
	}
};

// The SPS VUI fields pic_timing cannot be read or written without (ITU-T H.264 E.2.1 / E.2.2).
// Without hrd_parameters() CpbDpbDelaysPresentFlag is 0 and time_offset_length is 24.
struct H264SeiSpsContext
{
	bool cpb_dpb_delays_present		 = false;
	uint8_t cpb_removal_delay_length = 24;
	uint8_t dpb_output_delay_length	 = 24;
	uint8_t time_offset_length		 = 24;
	bool pic_struct_present			 = false;

	// Needed to work out pic_struct and ct_type (ITU-T H.264 7.4.2.1.1, D.2.2)
	bool frame_mbs_only			 = true;
	uint8_t log2_max_frame_num	 = 4;
	bool separate_colour_plane	 = false;

	bool IsValid() const
	{
		// H.264 codes these lengths in u(5), so 32 bits is the ceiling
		return (cpb_removal_delay_length <= 32) &&
			   (dpb_output_delay_length <= 32) &&
			   (time_offset_length <= 32);
	}

	ov::String GetInfoString() const
	{
		return ov::String::FormatString(
			"CpbDpbDelaysPresent:%s, CpbRemovalDelayLength:%u, DpbOutputDelayLength:%u, TimeOffsetLength:%u, PicStructPresent:%s, FrameMbsOnly:%s",
			cpb_dpb_delays_present ? "true" : "false",
			cpb_removal_delay_length, dpb_output_delay_length, time_offset_length,
			pic_struct_present ? "true" : "false",
			frame_mbs_only ? "true" : "false");
	}
};

struct H264SeiTimecodeZone
{
	// "UTC", "Z" or an empty string, "Local", or a fixed offset written "+09:00", "-0500" or
	// "+09". False when the text is none of those, and `zone` is then left as it was.
	static bool Parse(const ov::String &text, H264SeiTimecodeZone &zone);

	// Round trips through Parse()
	ov::String ToString() const;

	// The zone the server itself runs in, DST included
	bool local = false;
	// Ignored while `local` is set. UTC is an offset of zero, and no offset observes DST.
	int32_t offset_seconds = 0;
};

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

	// sei_rbsp() (ITU-T H.264 7.3.2.3.1). One SEI NAL may carry several sei_messages, split here
	// as raw payloads so one can be rewritten and the rest re-emitted verbatim.
	//
	// RBSP in and out: no NAL header, no emulation prevention bytes.
	struct Message
	{
		// Kept as the raw value instead of PayloadType, because payloadType uses the 0xFF
		// continuation scheme and may exceed 255. Re-emitting must be lossless.
		uint32_t payload_type = static_cast<uint32_t>(PayloadType::UNKNOWN);
		std::shared_ptr<ov::Data> payload;

		bool Is(PayloadType type) const
		{
			return payload_type == static_cast<uint32_t>(type);
		}
	};

	static bool SplitMessages(const std::shared_ptr<const ov::Data> &rbsp, std::vector<Message> &messages);
	static std::shared_ptr<ov::Data> JoinMessages(const std::vector<Message> &messages);

	// pic_timing (payloadType 1, ITU-T H.264 D.1.2). Every field's presence and bit width comes
	// from the SPS VUI, so the context is mandatory both ways.
	static bool ParsePicTiming(const std::shared_ptr<const ov::Data> &payload,
							   const H264SeiSpsContext &sps_context,
							   H264SeiPicTiming &pic_timing);
	static std::shared_ptr<ov::Data> SerializePicTiming(const H264SeiPicTiming &pic_timing,
														const H264SeiSpsContext &sps_context);


	H264SEI() = default;
	~H264SEI() = default;

	std::shared_ptr<ov::Data> Serialize();

	void SetPayloadType(PayloadType payload_type)
	{
		_payload_type = payload_type;
	}

	void SetPayloadData(const std::shared_ptr<ov::Data> &payload_data)
	{
		_payload_data = payload_data;
	}

	ov::String GetInfoString();

private:
	PayloadType _payload_type = PayloadType::USER_DATA_UNREGISTERED;
	std::shared_ptr<ov::Data> _payload_data = nullptr;
	uint8_t _rbsp_trailing_bits = 0x80;
};
