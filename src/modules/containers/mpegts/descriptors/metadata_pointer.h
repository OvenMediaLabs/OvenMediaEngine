//==============================================================================
//
//  Metadata_pointer_descriptor
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "descriptor.h"

namespace mpegts
{
	// It is only used for Timed Metadata now, so it is not fully implemented.
	// https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/HTTP_Live_Streaming_Metadata_Spec/2/2.html

	// ITU-T H.222.0 / 2.6.58 Metadata pointer descriptor
	class MetadataPointerDescriptor : public Descriptor
	{
	public:
		MetadataPointerDescriptor()
			: Descriptor(37) {}

		// Getter / Setter
		void SetMetadataApplicationFormat(uint16_t metadata_application_format)
		{
			_metadata_application_format = metadata_application_format;
		}
		uint16_t GetMetadataApplicationFormat() const
		{
			return _metadata_application_format;
		}

		void SetMetadataApplicationFormatIdentifier(uint32_t metadata_application_format_identifier)
		{
			_metadata_application_format = 0xFFFF;
			_metadata_application_format_identifier = metadata_application_format_identifier;
		}
		uint32_t GetMetadataApplicationFormatIdentifier() const
		{
			return _metadata_application_format_identifier;
		}

		void SetMetadataFormat(uint8_t metadata_format)
		{
			_metadata_format = metadata_format;
		}
		uint8_t GetMetadataFormat() const
		{
			return _metadata_format;
		}

		void SetMetadataFormatIdentifier(uint32_t metadata_format_identifier)
		{
			_metadata_format = 0xFF;
			_metadata_format_identifier = metadata_format_identifier;
		}
		uint32_t GetMetadataFormatIdentifier() const
		{
			return _metadata_format_identifier;
		}

		void SetMetadataServiceId(uint8_t metadata_service_id)
		{
			_metadata_service_id = metadata_service_id;
		}
		uint8_t GetMetadataServiceId() const
		{
			return _metadata_service_id;
		}

		// Only supports 0
		void SetMetadataLocatorRecordFlag(uint8_t metadata_locator_record_flag)
		{
			_metadata_locator_record_flag = metadata_locator_record_flag;
		}
		uint8_t GetMetadataLocatorRecordFlag() const
		{
			return _metadata_locator_record_flag;
		}

		// Only supports 0
		void SetMpegCarriageFlag(uint8_t mpeg_carriage_flag)
		{
			_mpeg_carriage_flag = mpeg_carriage_flag;
		}
		uint8_t GetMpegCarriageFlag() const
		{
			return _mpeg_carriage_flag;
		}

		void SetProgramNumber(uint8_t program_number)
		{
			_program_number = program_number;
		}
		uint8_t GetProgramNumber() const
		{
			return _program_number;
		}

		void SetPrivateData(const std::shared_ptr<const ov::Data> &private_data)
		{
			_private_data = private_data;
		}
		std::shared_ptr<const ov::Data> GetPrivateData() const
		{
			return _private_data;
		}

	protected:
		std::shared_ptr<const ov::Data> GetData() const override
		{
			ov::ByteStream stream(188);

			stream.WriteBE16(_metadata_application_format);
			if (_metadata_application_format == 0xFFFF)
			{
				stream.WriteBE32(_metadata_application_format_identifier);
			}

			stream.WriteBE(_metadata_format);
			if (_metadata_format == 0xFF)
			{
				stream.WriteBE32(_metadata_format_identifier);
			}

			stream.WriteBE(_metadata_service_id);

			if (_metadata_locator_record_flag != 0)
			{
				OV_ASSERT(false, "It only supports 0 for metadata_locator_record_flag, but it is %d", _metadata_locator_record_flag);
				return nullptr;
			}

			if (_mpeg_carriage_flag != 0)
			{
				OV_ASSERT(false, "It only supports 0 for mpeg_carriage_flag, but it is %d", _mpeg_carriage_flag);
				return nullptr;
			}

			uint8_t next_byte = (_metadata_locator_record_flag << 7) | (_mpeg_carriage_flag << 6) | _reserved;
			stream.WriteBE(next_byte);

			if (_mpeg_carriage_flag <= 2)
			{
				stream.WriteBE(_program_number);
			}

			if (_private_data != nullptr)
			{
				stream.Write(_private_data);
			}

			return stream.GetDataPointer();
		}

	private:
		// Default values are set for Timed Metadata
		uint16_t _metadata_application_format = 0xFFFF;
		uint32_t _metadata_application_format_identifier = 0x49443320;	// "ID3 "
		uint8_t _metadata_format = 0xFF;
		uint32_t _metadata_format_identifier = 0x49443320;	// "ID3 "
		uint8_t _metadata_service_id;						// typically 0
		uint8_t _metadata_locator_record_flag;				// 1 bit : only 0 supported
		uint8_t _mpeg_carriage_flag;						// 2 bit, only 0 supported
		uint8_t _reserved;									// 5 bit, 0x1f
		uint8_t _program_number;							// program number of the program whose es descriptor loop contains the metadata_descriptor

		std::shared_ptr<const ov::Data> _private_data;
	};
}  // namespace mpegts