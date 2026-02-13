#include "rtp_depacketizer_mpeg4_generic_audio.h"

#include <base/ovlibrary/bit_reader.h>
#include <modules/bitstream/aac/aac_adts.h>

#define OV_LOG_TAG "RtpDepacketizerMpeg4GenericAudio"

std::shared_ptr<ov::Data> RtpDepacketizerMpeg4GenericAudio::ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list)
{
	if (_aac_config.IsValid() == false)
	{
		return nullptr;
	}

	if (payload_list.size() <= 0)
	{
		return nullptr;
	}

	// One or more complete AU
	if (payload_list.size() == 1)
	{
		return Convert(payload_list.at(0), true);
	}
	// Fragmented AU
	else
	{
		auto reserve_size = 0;
		for (auto &payload : payload_list)
		{
			reserve_size += payload->GetLength();
			reserve_size += 16;	 // spare
		}

		auto bitstream = std::make_shared<ov::Data>(reserve_size);
		bool need_to_prepend_adts_header = true;
		for (const auto &payload : payload_list)
		{
			auto raw_data = Convert(payload, false);
			if (raw_data == nullptr)
			{
				return nullptr;
			}

			// Some RTSP servers send AAC packets with ADTS header,
			// so check if an ADTS header is already present before adding an ADTS header. (#1897)
			if (need_to_prepend_adts_header && (raw_data->GetLength() >= 2))
			{
				auto raw_data_ptr = raw_data->GetDataAs<uint8_t>();
				
				// Fast check for ADTS header
				if ((raw_data_ptr[0] == 0xFF) && ((raw_data_ptr[1] & 0xF6) == 0xF0))
				{
					// Precise check for ADTS header
					AACAdts adts;

					if (AACAdts::Parse(raw_data_ptr, raw_data->GetLength(), adts))
					{
						// If the ADTS header is already present, do not prepend it again.
						need_to_prepend_adts_header = false;
					}
				}
			}

			if (need_to_prepend_adts_header)
			{
				if (payload->GetLength() < 4)
				{
					logte("Too small rtp payload to parse MPEG4-Generic audio data");
					return nullptr;
				}

				// Get AU size
				auto payload_ptr = payload->GetDataAs<uint8_t>();
				BitReader parser(payload_ptr, payload->GetLength());
				[[maybe_unused]] uint16_t headers_len = parser.ReadBits<uint16_t>(16);

				// (RFC) AU-size is associated with an AU fragment, the AU size indicates
				// the size of the entire AU and not the size of the fragment.
				uint16_t raw_aac_data_length = parser.ReadBits<uint16_t>(_size_length);

				//Get the AACSecificConfig value from extradata;
				uint8_t aac_profile = static_cast<uint8_t>(_aac_config.GetAacProfile());
				uint8_t aac_sample_rate = static_cast<uint8_t>(_aac_config.SamplingFrequencyIndex());
				uint8_t aac_channels = static_cast<uint8_t>(_aac_config.Channel());

				bitstream->Append(AacConverter::MakeAdtsHeader(aac_profile, aac_sample_rate, aac_channels, raw_aac_data_length));

				need_to_prepend_adts_header = false;
			}

			bitstream->Append(raw_data);
		}

		return bitstream;
	}

	return nullptr;
}

// OME does not support interleaving as it is an ultra-low latency streaming server.
bool RtpDepacketizerMpeg4GenericAudio::SetConfigParams(RtpDepacketizerMpeg4GenericAudio::Mode mode, uint32_t size_length, uint32_t index_length, uint32_t index_delta_length, const std::shared_ptr<ov::Data> &config)
{
	_mode = mode;
	_size_length = size_length;
	_index_length = index_length;
	_index_delta_length = index_delta_length;

	if (config == nullptr)
	{
		return false;
	}

	return _aac_config.Parse(config);
}

std::shared_ptr<ov::Data> RtpDepacketizerMpeg4GenericAudio::Convert(const std::shared_ptr<const ov::Data> &payload, bool include_adts_header)
{
	auto aac_data = std::make_shared<ov::Data>(payload->GetLength() * 2);
	auto const payload_ptr = payload->GetDataAs<uint8_t>();
	const size_t payload_len = payload->GetLength();

	/*
	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- .. -+-+-+-+-+-+-+-+-+-+
      |AU-headers-length|AU-header|AU-header|      |AU-header|padding|
      |                 |   (1)   |   (2)   |      |   (n)   | bits  |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- .. -+-+-+-+-+-+-+-+-+-+
	*/

	BitReader parser(payload_ptr, payload_len);

	// the AU-headers-length is a two octet field that specifies the length in bits
	// of the immediately following AU-headers, excluding the padding bits.
	uint16_t au_headers_len = parser.ReadBits<uint16_t>(16);
	size_t header_section_offset = 0;

	// (RFC) If the concatenated AU-headers consume a non-integer number of octets,
	// up to 7 zero-padding bits MUST be inserted at the end in order to achieve
	// octet-alignment of the AU Header Section.

	// Therefore, by dividing by +7, bytes according to padding can be calculated.
	uint16_t au_headers_bytes = (au_headers_len + 7) / 8;

	// In this mode, the RTP payload consists of the AU Header Section,
	// followed by either one AAC frame, several concatenated AAC frames or
	// one fragmented AAC frame.  The Auxiliary Section MUST be empty.  For
	// each AAC frame contained in the payload, there MUST be an AU-header
	// in the AU Header Section to provide:
	auto data_section_ptr = payload_ptr + (2 + au_headers_bytes);
	size_t data_section_length = payload_len - (2 + au_headers_bytes);

	size_t data_section_offset = 0;
	uint32_t au_number = 0;
	while (header_section_offset < au_headers_len)
	{
		// Indicates the size in octets
		uint16_t au_size = parser.ReadBits<uint16_t>(_size_length);
		header_section_offset += _size_length;
		[[maybe_unused]] uint8_t au_index = 0;
		[[maybe_unused]] uint8_t au_index_delta = 0;

		// First header
		if (au_number == 0)
		{
			au_index = parser.ReadBits<uint8_t>(_index_length);
			header_section_offset += _index_length;
		}
		else
		{
			au_index_delta = parser.ReadBits<uint8_t>(_index_delta_length);
			header_section_offset += _index_delta_length;
		}

		logtt("MPEG4-Generic Audio Parser : header_number(%d) au_size(%d) au_index(%d) au_index_delta(%d) au_headers_len(%d)", au_number, au_size, au_index, au_index_delta, au_headers_len);

		// Extract raw aac data and convert to ADTS
		if (include_adts_header == true)
		{
			// Some RTSP servers send AAC packets with ADTS header,
			// so check if an ADTS header is already present before adding an ADTS header. (#1897)
			auto au_data		 = data_section_ptr + data_section_offset;
			bool has_adts_header = false;

			if (au_size >= 2)
			{
				// Fast check for ADTS header
				if ((au_data[0] == 0xFF) && ((au_data[1] & 0xF6) == 0xF0))
				{
					// Precise check for ADTS header
					AACAdts adts;

					has_adts_header = AACAdts::Parse(au_data, au_size, adts);
				}
			}

			if (has_adts_header == false)
			{
				auto adts_data = AacConverter::ConvertRawToAdts(data_section_ptr + data_section_offset, au_size, _aac_config);

				if (adts_data == nullptr)
				{
					logte("Could not extract raw aac data in mpeg4-generic audio");
					return nullptr;
				}

				aac_data->Append(adts_data);
			}
			else
			{
				aac_data->Append(au_data, au_size);
			}
		}
		else
		{
			// When there is no ADTS header, only one AU must exist in the rtp payload.
			if (data_section_offset != 0)
			{
				logte("RTP packet should carry a single fragment of one AU");
				return nullptr;
			}

			// (RFC) In this case, the size of the fragment is known from the size of the AU data section.
			aac_data->Append(data_section_ptr, data_section_length);
			break;
		}

		data_section_offset += au_size;
		au_number++;
	}

	return aac_data;
}