//==============================================================================
//
//  descriptor
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/ovlibrary/ovlibrary.h>

#define DESCRIPTOR_HEADER_SIZE	2

namespace mpegts
{	
	class Descriptor
	{
	public:
		Descriptor(uint8_t tag, const std::shared_ptr<ov::Data> &data)
		{
			_tag = tag;
			_data = data;
		}
		virtual ~Descriptor() = default;

		std::shared_ptr<const ov::Data> Build()
		{
			if (_tag == 0 || _tag == 1)
			{
				// invalid tag
				OV_ASSERT(false, "Invalid tag(%d)", _tag);
				return nullptr;
			}

			auto data = GetData();
			if (data == nullptr)
			{
				OV_ASSERT(false, "Failed to get descriptor data");
				return nullptr;
			}

			if (data->GetLength() > 255)
			{
				OV_ASSERT(false, "Descriptor data length is too long(%zu)", data->GetLength());
				return nullptr;
			}

			ov::ByteStream stream(188);

			stream.WriteBE(_tag);
			stream.WriteBE(static_cast<uint8_t>(data->GetLength()));
			stream.Write(data);

			return stream.GetDataPointer();
		}

		static std::shared_ptr<Descriptor> Parse(BitReader *parser)
		{
			if(parser->BytesRemained() < DESCRIPTOR_HEADER_SIZE)
			{
				OV_ASSERT(false, "Not enough bytes remained");
				return nullptr;
			}

			auto tag = parser->ReadBytes<uint8_t>();
			auto length = parser->ReadBytes<uint8_t>();

			if (parser->BytesRemained() < length)
			{
				OV_ASSERT(false, "Not enough bytes remained for descriptor data");
				return nullptr;
			}

			auto data = std::make_shared<ov::Data>(parser->CurrentPosition(), length);
			parser->SkipBytes(length);

			return std::make_shared<Descriptor>(tag, data);
		}

		uint8_t GetTag() const
		{
			return _tag;
		}

		uint8_t GetLength() const
		{
			auto data = GetData();
			if (data == nullptr)
			{
				return 0;
			}
			return data->GetLength();
		}

		uint8_t GetPacketLength() const
		{
			return GetLength() + DESCRIPTOR_HEADER_SIZE;
		}

		virtual std::shared_ptr<const ov::Data> GetData() const
		{
			return _data;
		}

	protected:
		Descriptor(uint8_t tag)
		{
			_tag = tag;
		}

	private:
		uint8_t		_tag = 0; // 8bits
		// uint8_t 	_length = 0; // 8bits
		std::shared_ptr<ov::Data> _data = nullptr; // temporary parsed data, it will be removed after implementing Parse()
	};
}