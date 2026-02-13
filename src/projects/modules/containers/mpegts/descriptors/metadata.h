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

	// ITU-T H.222.0 / 2.6.60 Metadata descriptor
	class MetadataDescriptor : public Descriptor
	{
	public:
		MetadataDescriptor()
			: Descriptor(38) {}

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
		
		// It only supports 0 
		void SetDecoderConfigFlags(uint8_t decoder_config_flags)
		{
			_decoder_config_flags = decoder_config_flags;
		}
		uint8_t GetDecoderConfigFlags() const
		{
			return _decoder_config_flags;
		}

		// It only supports 0
		void SetDsmCcFlag(uint8_t dsm_cc_flag)
		{
			_dsm_cc_flag = dsm_cc_flag;
		}
		uint8_t GetDsmCcFlag() const
		{
			return _dsm_cc_flag;
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

			if (_decoder_config_flags != 0)
			{
				OV_ASSERT(false, "It only supports 0 for decoder_config_flags, but it is %d", _decoder_config_flags);
				return nullptr;
			}

			if (_dsm_cc_flag != 0)
			{
				OV_ASSERT(false, "It only supports 0 for dsm_cc_flag, but it is %d", _dsm_cc_flag);
				return nullptr;
			}

			uint8_t next_byte = (_decoder_config_flags << 5) | (_dsm_cc_flag << 4) | _reserved;
			stream.WriteBE(next_byte);

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
		uint8_t _metadata_service_id = 0; // typically 0
		uint8_t _decoder_config_flags = 0; // 3 bit, only supports 0
		uint8_t _dsm_cc_flag = 0; // 1 bit, only supports 0
		uint8_t _reserved = 0xF; // 4 bit, 0xF						

		std::shared_ptr<const ov::Data> _private_data;

	};
}  // namespace mpegts
