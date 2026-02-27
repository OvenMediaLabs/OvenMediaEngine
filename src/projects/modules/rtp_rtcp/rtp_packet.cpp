#include "rtp_packet.h"
#include "base/ovlibrary/byte_io.h"

RtpPacket::RtpPacket()
{
	_marker = false;
	_payload_type = 0;
	_origin_payload_type = 0;
	_is_fec = false;
	_sequence_number = 0;
	_timestamp = 0;
	_ssrc = 0;
	_payload_offset = FIXED_HEADER_SIZE;
	_payload_size = 0;
	_padding_size = 0;
	_extension_size = 0;

	_data = std::make_shared<ov::Data>(RTP_DEFAULT_MAX_PACKET_SIZE);
	_data->SetLength(FIXED_HEADER_SIZE);
	_buffer = _data->GetWritableDataAs<uint8_t>();

	_buffer[0] = RTP_VERSION << 6;

	_ntp_timestamp = 0;
	_created_time = std::chrono::system_clock::now();

	_is_available = true;
}

RtpPacket::RtpPacket(const std::shared_ptr<const ov::Data> &data)
{
	_is_available = Parse(data);
}

RtpPacket::RtpPacket(const RtpPacket &src)
{
	_marker = src._marker;
	_payload_type = src._payload_type;
	_origin_payload_type = src._origin_payload_type;
	_ssrc = src._ssrc;
	_payload_offset = src._payload_offset;
	_payload_size = src._payload_size;
	_padding_size = src._padding_size;
	_extension_size = src._extension_size;
	_sequence_number = src._sequence_number;
	_timestamp = src._timestamp;
	_extension_size = src._extension_size;
	_extensions = src._extensions;
	_extension_buffer_offset = src._extension_buffer_offset;
	_extension_type = src._extension_type;
	_data = src._data->Clone();
	_buffer = _data->GetWritableDataAs<uint8_t>();

	// Extra Data
	_track_id = src._track_id;
	_ntp_timestamp = src._ntp_timestamp;
	_is_keyframe = src._is_keyframe;
	_is_first_packet_of_frame = src._is_first_packet_of_frame;
	_is_video_packet = src._is_video_packet;
	_rtsp_channel = src._rtsp_channel;
	_created_time = std::chrono::system_clock::now();

	_is_available = true;
}

RtpPacket::~RtpPacket()
{

}

ov::String RtpPacket::Dump()
{
	if(_is_available == false)
	{
		return ov::String::FormatString("Invalid packet");
	}

	return ov::String::FormatString("RTP Packet - size(%zu) p(%d) x(%d) cc(%d) m(%d) pt(%d) seq_no(%d) timestamp(%u) ssrc(%u) extension_size(%zu) payload_offset(%zu), payload_size(%zu) padding_size(%d)", _data->GetLength(), _has_padding, _has_extension, _cc, _marker, _payload_type, _sequence_number, _timestamp, _ssrc, _extension_size, _payload_offset, _payload_size, _padding_size);
}

bool RtpPacket::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if(data->GetLength() < FIXED_HEADER_SIZE)
	{
		// Wrong data
		return false;
	}

	auto buffer = data->GetDataAs<uint8_t>();
	auto buffer_size = data->GetLength();

	uint8_t version = buffer[0] >> 6;
	if(version != RTP_VERSION)
	{
		// This data is not RTP packet
		return false;
	}

	_has_padding = (buffer[0] & 0x20) != 0;
	_has_extension = (buffer[0] & 0x10) != 0;
	_cc = buffer[0] & 0x0f;

	// It is specific values only for OME
	_is_fec = false;
	_origin_payload_type = 0;

	// Marker
	_marker = (buffer[1] & 0x80) != 0;
	// PT
	_payload_type = buffer[1] & 0x7f;
	// Sequence Number
	_sequence_number = ByteReader<uint16_t>::ReadBigEndian(&buffer[2]);
	// Timestamp
	_timestamp = ByteReader<uint32_t>::ReadBigEndian(&buffer[4]);
	// SSRC
	_ssrc = ByteReader<uint32_t>::ReadBigEndian(&buffer[8]);

	//TODO(Getroot): Parse CSRC
	_payload_offset = FIXED_HEADER_SIZE + (_cc * 4);
	if(_has_padding)
	{
		_padding_size = buffer[buffer_size -1];
		if(_padding_size == 0)
		{
			return false;
		}
	}
	else
	{
		_padding_size = 0;
	}

	_extension_size = 0;
	if(_has_extension)
	{
		/*
		https://tools.ietf.org/html/rfc3550#section-5.3.1

		16-bit length field that counts the number of 32-bit words in the extension, 
		excluding the four-octet extension header

		0                   1                   2                   3
		0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		|      defined by profile       |           length              |
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		|                        header extension                       |
		|                             ....                              |
		*/
		size_t extension_offset = _payload_offset + 4;
		if(extension_offset > buffer_size)
		{
			return false;
		}

		uint16_t extension_profile = ByteReader<uint16_t>::ReadBigEndian(&buffer[_payload_offset]);
		_extension_size = ByteReader<uint16_t>::ReadBigEndian(&buffer[_payload_offset + 2]) * 4;

		if(extension_offset + _extension_size > buffer_size)
		{
			return false;
		}

		_payload_offset = extension_offset + _extension_size;

		while(extension_offset < _payload_offset)
		{
			// Padding
			if(buffer[extension_offset] == 0)
			{
				extension_offset ++;
				continue;
			}

			uint8_t id = 0;
			uint8_t len = 0;

			// One Byte Header
			if(extension_profile == ONE_BYTE_EXTENSION_ID)
			{
				auto header = buffer[extension_offset++];
				id = header >> 4;

				// https://datatracker.ietf.org/doc/html/rfc8285#section-4.2
				// The 4-bit length is the number, minus one, of data bytes of this
   				// header extension element following the one-byte header.
				len = (header & 0xF) + 1;
			}
			// Two Byte Header
			else
			{
				id = buffer[extension_offset++];
				len = buffer[extension_offset++];
			}

			_extensions.emplace(id, ov::Data(&buffer[extension_offset], len, false));
			extension_offset += len;
		}
	}

	if(_payload_offset + _padding_size > buffer_size)
	{
		return false;
	}

	_payload_size = buffer_size - _payload_offset - _padding_size;

	// Full data
	_data = data->Clone();
	_buffer = _data->GetWritableDataAs<uint8_t>();

	_created_time = std::chrono::system_clock::now();

	return true;
}

std::shared_ptr<ov::Data> RtpPacket::GetData() const
{
	return _data;
}

size_t RtpPacket::GetDataLength() const
{
	return _data == nullptr ? 0 : _data->GetLength();
}

// Getter
bool RtpPacket::Marker() const
{
	return _marker;
}

bool RtpPacket::IsUlpfec() const
{
	return _is_fec;
}

uint8_t RtpPacket::PayloadType() const
{
	return _payload_type;
}

uint8_t RtpPacket::OriginPayloadType() const
{
	return _origin_payload_type;
}

uint16_t RtpPacket::SequenceNumber() const
{
	return _sequence_number;
}
uint32_t RtpPacket::Timestamp() const
{
	return _timestamp;
}
uint32_t RtpPacket::Ssrc() const
{
	return _ssrc;
}
std::vector<uint32_t> RtpPacket::Csrcs() const
{
	// Extract the value in the lower 4 bits of the first byte
	size_t num_csrc = _buffer[0] & 0x0F;
	std::vector<uint32_t> csrcs(num_csrc);
	for (size_t i = 0; i < num_csrc; ++i)
	{
		// 4 bytes char array => 4 bytes uint32_t
		csrcs[i] = ByteReader<uint32_t>::ReadBigEndian(&_buffer[FIXED_HEADER_SIZE + i * 4]);
	}

	return csrcs;
}

std::map<uint8_t, ov::Data> RtpPacket::Extensions() const
{
	return _extensions;
}

std::optional<ov::Data>	RtpPacket::GetExtension(uint8_t id) const
{
	auto it = _extensions.find(id);
	if(it == _extensions.end())
	{
		return {};
	}
	
	return it->second;
}

uint8_t* RtpPacket::Buffer() const
{
	return &_buffer[0];
}

// Setter
void RtpPacket::SetMarker(bool marker_bit)
{
	_marker = marker_bit;

	if (_marker)
	{
		// Make the first 1 bit 1
		_buffer[1] = _buffer[1] | 0x80;
	}
	else
	{
		// Make the first 1 bit 0.
		_buffer[1] = _buffer[1] & 0x7F;
	}
}

void RtpPacket::SetPayloadType(uint8_t payload_type)
{
	_payload_type = payload_type;
	_buffer[1] = (_buffer[1] & 0x80) | payload_type;
}

void RtpPacket::SetUlpfec(bool is_fec, uint8_t origin_payload_type)
{
	_is_fec = is_fec;
	_origin_payload_type = origin_payload_type;
}

void RtpPacket::SetSequenceNumber(uint16_t seq_no)
{
	_sequence_number = seq_no;
	ByteWriter<uint16_t>::WriteBigEndian(&_buffer[2], seq_no);
}

void RtpPacket::SetTimestamp(uint32_t timestamp)
{
	_timestamp = timestamp;
	ByteWriter<uint32_t>::WriteBigEndian(&_buffer[4], timestamp);
}

void RtpPacket::SetSsrc(uint32_t ssrc)
{
	_ssrc = ssrc;
	ByteWriter<uint32_t>::WriteBigEndian(&_buffer[8], ssrc);
}

void RtpPacket::SetCsrcs(const std::vector<uint32_t>& csrcs)
{
	// TODO: Validation check
	// can only be inserted when there is no sub-data. Because of buffer cleanup, you have to push it all back to do this
	// Is there no Extention?
	// Is there no payload?
	// Is there no Padding?
	// Is there no more than 15 csrcs? (RFC)
	// Isn't there insufficient buffer reserve?
	_payload_offset = FIXED_HEADER_SIZE + 4 * csrcs.size();

	// Adjust _buffer size
	_data->SetLength(_payload_offset);
	_buffer = _data->GetWritableDataAs<uint8_t>();

	// Enter csrs size in the lower 4 bits of the first byte
	_cc = csrcs.size();
	_buffer[0] = (_buffer[0] & 0xF0) | _cc;

	size_t offset = FIXED_HEADER_SIZE;
	for (uint32_t csrc : csrcs)
	{
		ByteWriter<uint32_t>::WriteBigEndian(&_buffer[offset], csrc);
		offset += 4;
	}
}

void RtpPacket::SetExtensions(const RtpHeaderExtensions& extensions)
{
	// TODO(Getroot) : Add item to _extensions

	auto extension_length = extensions.GetTotalDataLength();
	auto pad_length = (4 - (extension_length % 4)) % 4;

	extension_length += pad_length;

	// Payload offset = RTP Header(12) + CSRCs + Extension Header(4) + Extensions
	_payload_offset = FIXED_HEADER_SIZE + (_cc * 4) + EXTENSION_HEADER_SIZE + extension_length;
	_data->SetLength(_payload_offset);
	_buffer = _data->GetWritableDataAs<uint8_t>();

	// Set X bit to 1
	_has_extension = true;
	_buffer[0] = _buffer[0] | 0x10;

	auto offset = FIXED_HEADER_SIZE + (_cc * 4);

	_extension_type = extensions.GetHeaderType();
	if(_extension_type == RtpHeaderExtension::HeaderType::ONE_BYTE_HEADER)
	{
		// Write profile (Use Two Byte Header)
		ByteWriter<uint8_t>::WriteBigEndian(&_buffer[offset], 0xBE);
		ByteWriter<uint8_t>::WriteBigEndian(&_buffer[offset + 1], 0xDE);
	}
	else
	{
		// Write profile (Use Two Byte Header)
		ByteWriter<uint8_t>::WriteBigEndian(&_buffer[offset], 0x10);
		ByteWriter<uint8_t>::WriteBigEndian(&_buffer[offset + 1], 0x00);
	}
	offset += 2;

	// Wirte Length
	ByteWriter<uint16_t>::WriteBigEndian(&_buffer[offset], extension_length / 4);
	offset += 2;

	// Write Extensions
	auto extensions_map = extensions.GetMap();
	for(const auto &[id, extension] : extensions_map)
	{
		_extension_buffer_offset[id] = offset;

		auto extension_data = extension->Marshal(extensions.GetHeaderType());
		memcpy(&_buffer[offset], extension_data->GetData(), extension_data->GetLength());
		offset += extension_data->GetLength();
	}

	// Set padding
	memset(&_buffer[offset], 0, pad_length);
	offset += pad_length;
}

size_t RtpPacket::HeadersSize() const
{
	return _payload_offset;
}

size_t RtpPacket::PayloadSize() const
{
	return _payload_size;
}

size_t RtpPacket::PaddingSize() const
{
	return _padding_size;
}

size_t RtpPacket::ExtensionSize() const
{
	return _extension_size;
}

bool RtpPacket::HasExtension() const
{
	return _has_extension;
}

bool RtpPacket::HasPadding() const
{
	return _has_padding;
}

bool RtpPacket::SetPayload(const uint8_t *payload, size_t payload_size)
{
	auto payload_buffer = SetPayloadSize(payload_size);
	if(payload_buffer == nullptr)
	{
		loge("RtpPacket", "Cannot set payload size");
		return false;
	}

	memcpy(payload_buffer, payload, payload_size);

	return true;
}

uint8_t* RtpPacket::SetPayloadSize(size_t size_bytes)
{
	if (_payload_offset + size_bytes > _data->GetCapacity())
	{
		OV_ASSERT(false, "Data capacity must be greater than %ld (packet : %ld)", _data->GetCapacity(), _payload_offset + size_bytes);
		return nullptr;
	}

	_payload_size = size_bytes;
	_data->SetLength(_payload_offset + _payload_size);
	_buffer = _data->GetWritableDataAs<uint8_t>();

	return &_buffer[_payload_offset];
}

uint8_t* RtpPacket::AllocatePayload(size_t size_bytes)
{
	return SetPayloadSize(size_bytes);
}

uint8_t* RtpPacket::Header() const
{
	return &_buffer[0];
}

uint8_t* RtpPacket::Payload() const
{
	return &_buffer[_payload_offset];
}

uint8_t* RtpPacket::Extension(uint8_t id) const
{ 
	auto it = _extension_buffer_offset.find(id);
	if (it == _extension_buffer_offset.end())
	{
		return nullptr;
	}

	auto offset = it->second;

	return &_buffer[offset];
}

std::chrono::system_clock::time_point RtpPacket::GetCreatedTime()
{
	return _created_time;
}