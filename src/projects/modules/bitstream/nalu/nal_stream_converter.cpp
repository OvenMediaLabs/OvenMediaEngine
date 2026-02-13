#include "nal_stream_converter.h"

#define OV_LOG_TAG "NalStreamConverter"

static uint8_t START_CODE[4] = {0x00, 0x00, 0x00, 0x01};

std::shared_ptr<ov::Data> NalStreamConverter::ConvertXvccToAnnexb(const std::shared_ptr<const ov::Data> &data)
{
	auto annexb_data = std::make_shared<ov::Data>(data->GetLength() + (data->GetLength() / 2));

	ov::ByteStream read_stream(data.get());

	while (read_stream.Remained() > 0)
	{
		if (read_stream.IsRemained(4) == false)
		{
			logte("Not enough data to parse NAL");
			return nullptr;
		}

		size_t nal_length = read_stream.ReadBE32();

		if (read_stream.IsRemained(nal_length) == false)
		{
			logte("NAL length (%d) is greater than buffer length (%d)", nal_length, read_stream.Remained());
			return nullptr;
		}

		auto nal_data = read_stream.GetRemainData()->Subdata(0LL, nal_length);
		[[maybe_unused]] auto skipped = read_stream.Skip(nal_length);
		OV_ASSERT2(skipped == nal_length);

		annexb_data->Append(START_CODE, sizeof(START_CODE));
		annexb_data->Append(nal_data);
	}

	return annexb_data;
}

static inline int GetStartPatternSize(const void *data, size_t length, int first_pattern_size)
{
	if (data != nullptr)
	{
		auto buffer = static_cast<const uint8_t *>(data);

		if (first_pattern_size > 0)
		{
			if ((buffer[0] == 0x00) && (buffer[1] == 0x00))
			{
				if (
					(first_pattern_size == 4) &&
					((length >= 4) && (buffer[2] == 0x00) && (buffer[3] == 0x01)))
				{
					// 0x00 0x00 0x00 0x01 pattern
					return 4;
				}
				else if (
					(first_pattern_size == 3) &&
					((length >= 3) && (buffer[2] == 0x01)))
				{
					// 0x00 0x00 0x01 pattern
					return 3;
				}
			}

			// pattern_size must be the same with the first_pattern_size
			return -1;
		}
		else
		{
			// probe mode

			if ((length >= 4) && ((buffer[0] == 0x00) && (buffer[1] == 0x00) && (buffer[2] == 0x00) && (buffer[3] == 0x01)))
			{
				// 0x00 0x00 0x00 0x01 pattern
				return 4;
			}
			if ((length >= 3) && ((buffer[0] == 0x00) && (buffer[1] == 0x00) && (buffer[2] == 0x01)))
			{
				// 0x00 0x00 0x01 pattern
				return 3;
			}
		}
	}

	return -1;
}

std::shared_ptr<ov::Data> NalStreamConverter::ConvertAnnexbToXvcc(const std::shared_ptr<const ov::Data> &data)
{
	// size_t total_pattern_length = 0;

	auto buffer = data->GetDataAs<uint8_t>();
	size_t remained = data->GetLength();
	off_t offset = 0;
	off_t last_offset = 0;

	auto avcc_data = std::make_shared<ov::Data>(data->GetLength() + 1024);
	ov::ByteStream byte_stream(avcc_data);

	// This code assumes that (NALULengthSizeMinusOne == 3)
	while (remained > 0)
	{
		if (*buffer == 0x00)
		{
			auto pattern_size = GetStartPatternSize(buffer, remained, 0);

			if (pattern_size > 0)
			{
				if (last_offset < offset)
				{
					auto nalu = data->Subdata(last_offset, offset - last_offset);

					byte_stream.WriteBE32(nalu->GetLength());
					byte_stream.Write(nalu);

					last_offset = offset;
				}

				buffer += pattern_size;
				offset += pattern_size;
				last_offset += pattern_size;
				remained -= pattern_size;

				continue;
			}
		}

		buffer++;
		offset++;
		remained--;
	}

	if (last_offset < offset)
	{
		// Append remained data
		auto nalu = data->Subdata(last_offset, offset - last_offset);

		byte_stream.WriteBE32(nalu->GetLength());
		byte_stream.Write(nalu);

		last_offset = offset;
	}

	return avcc_data;
}

std::shared_ptr<ov::Data> NalStreamConverter::ConvertAnnexbToXvcc(const std::shared_ptr<const ov::Data> &data, const FragmentationHeader *frag_header)
{
	if (frag_header == nullptr)
	{
		return ConvertAnnexbToXvcc(data);
	}

	auto avcc_data = std::make_shared<ov::Data>(data->GetLength() + 1024);
	ov::ByteStream byte_stream(avcc_data);

	for (size_t i = 0; i < frag_header->fragmentation_offset.size(); i++)
	{
		auto offset = frag_header->fragmentation_offset[i];
		auto length = frag_header->fragmentation_length[i];

		// Use 4 bytes for NALU length
		byte_stream.WriteBE32(length);
		byte_stream.Write(data->GetDataAs<uint8_t>() + offset, length);
	}

	return avcc_data;
}