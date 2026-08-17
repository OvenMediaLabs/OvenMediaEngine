#include "ovt_packet.h"

#include <base/ovlibrary/byte_io.h>

OvtPacket::OvtPacket()
{
	_version		 = OVT_VERSION;
	_marker			 = 0;
	_payload_type	 = 0;
	_timestamp		 = 0;
	_sequence_number = 0;
	_session_id		 = 0;
	_payload_length	 = 0;

	_data			 = std::make_shared<ov::Data>();
	_data->Reserve(OVT_DEFAULT_MAX_PACKET_SIZE);
	_data->SetLength(OVT_FIXED_HEADER_SIZE);
	_buffer	   = _data->GetWritableDataAs<uint8_t>();

	_buffer[0] = OVT_VERSION << 6;
}

OvtPacket::OvtPacket(const ov::Data &data)
{
	Load(data);
}

OvtPacket::OvtPacket(OvtPacket &src)
{
	_version			 = OVT_VERSION;
	_marker				 = src._marker;
	_payload_type		 = src._payload_type;
	_timestamp			 = src._timestamp;
	_sequence_number	 = src._sequence_number;
	_session_id			 = src._session_id;
	_payload_length		 = src._payload_length;
	_is_packet_available = src._is_packet_available;

	_data				 = src._data->Clone();
	_data->SetLength(src._data->GetLength());
	_buffer = _data->GetWritableDataAs<uint8_t>();
}

OvtPacket::~OvtPacket()
{
}

bool OvtPacket::LoadHeader(const ov::Data &data)
{
	if (data.GetLength() < OVT_FIXED_HEADER_SIZE)
	{
		_is_packet_available = false;
		return false;
	}

	auto buffer = data.GetDataAs<uint8_t>();

	uint8_t version = (buffer[0] & 0xC0) >> 6;
	if (version != OVT_VERSION)
	{
		loge("ERROR", "Version : %d, %d", buffer[0], version);
		_is_packet_available = false;
		return false;
	}

	_data.reset();

	_data = std::make_shared<ov::Data>();
	_data->Reserve(OVT_DEFAULT_MAX_PACKET_SIZE);
	_data->SetLength(OVT_FIXED_HEADER_SIZE);
	_buffer = _data->GetWritableDataAs<uint8_t>();

	_buffer[0] = OVT_VERSION << 6;

	// Read header
	SetMarker(buffer[0] & 0x20);
	SetPayloadType(ByteReader<uint8_t>::ReadBigEndian(&buffer[1]));
	SetSequenceNumber(ByteReader<uint16_t>::ReadBigEndian(&buffer[2]));
	SetTimestamp(ByteReader<uint64_t>::ReadBigEndian(&buffer[4]));
	SetSessionId(ByteReader<uint32_t>::ReadBigEndian(&buffer[12]));

	uint16_t payload_length = ByteReader<uint16_t>::ReadBigEndian(&buffer[16]);
	if (payload_length > OVT_DEFAULT_MAX_PACKET_SIZE - OVT_FIXED_HEADER_SIZE)
	{
		loge("ERROR", "Invalid payload length : %u", payload_length);
		_is_packet_available = false;
		return false;
	}
	SetPayloadLength(payload_length);

	if (PayloadLength() == 0)
	{
		_is_packet_available = true;
	}
	else
	{
		// this packet is unavailable until setiing payload
		_is_packet_available = false;
	}

	return true;
}

bool OvtPacket::Load(const ov::Data &data)
{
	if (!LoadHeader(data))
	{
		return false;
	}

	if (data.GetLength() < static_cast<size_t>(OVT_FIXED_HEADER_SIZE + _payload_length))
	{
		// Invalid data
		_is_packet_available = false;
		return false;
	}

	auto buffer = data.GetDataAs<uint8_t>();
	SetPayload(&buffer[OVT_FIXED_HEADER_SIZE], _payload_length);

	_is_packet_available = true;

	return true;
}

bool OvtPacket::IsHeaderAvailable() const
{
	return _payload_type > 0;
}

bool OvtPacket::IsPacketAvailable() const
{
	return IsHeaderAvailable() && _is_packet_available;
}

uint8_t OvtPacket::Version() const
{
	return _version;
}

bool OvtPacket::Marker() const
{
	return _marker;
}

uint16_t OvtPacket::SequenceNumber() const
{
	return _sequence_number;
}

uint8_t OvtPacket::PayloadType() const
{
	return _payload_type;
}

uint64_t OvtPacket::Timestamp() const
{
	return _timestamp;
}

uint32_t OvtPacket::SessionId() const
{
	return _session_id;
}

uint32_t OvtPacket::PacketLength() const
{
	return OVT_FIXED_HEADER_SIZE + _payload_length;
}

uint16_t OvtPacket::PayloadLength() const
{
	return _payload_length;
}

const uint8_t *OvtPacket::Payload() const
{
	return &_buffer[OVT_FIXED_HEADER_SIZE];
}

const uint8_t *OvtPacket::GetBuffer() const
{
	return &_buffer[0];
}

const std::shared_ptr<ov::Data> &OvtPacket::GetData() const
{
	return _data;
}

size_t OvtPacket::GetDataLength() const
{
	return _data == nullptr ? 0 : _data->GetLength();
}

void OvtPacket::SetMarker(bool marker_bit)
{
	_marker = marker_bit;

	if (_marker)
	{
		_buffer[0] = _buffer[0] | 0x20;
	}
	else
	{
		_buffer[0] = _buffer[0] & 0xDF;
	}
}

void OvtPacket::SetPayloadType(uint8_t payload_type)
{
	_payload_type = payload_type;
	_buffer[1]	  = payload_type;
}

void OvtPacket::SetSequenceNumber(uint16_t sequence_number)
{
	_sequence_number = sequence_number;
	ByteWriter<uint16_t>::WriteBigEndian(&_buffer[2], _sequence_number);
}

void OvtPacket::SetTimestampNow()
{
	using namespace std::chrono;
	uint64_t timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	SetTimestamp(timestamp);
}

void OvtPacket::SetTimestamp(uint64_t timestamp)
{
	_timestamp = timestamp;
	ByteWriter<uint64_t>::WriteBigEndian(&_buffer[4], _timestamp);
}

void OvtPacket::SetSessionId(uint32_t session_id)
{
	_session_id = session_id;
	ByteWriter<uint32_t>::WriteBigEndian(&_buffer[12], _session_id);
}

void OvtPacket::SetPayloadLength(size_t payload_length)
{
	_payload_length = payload_length;
	ByteWriter<uint16_t>::WriteBigEndian(&_buffer[16], _payload_length);

	_data->SetLength(OVT_FIXED_HEADER_SIZE + payload_length);
	_buffer = _data->GetWritableDataAs<uint8_t>();
}

bool OvtPacket::SetPayload(const uint8_t *payload, size_t payload_length)
{
	if (OVT_FIXED_HEADER_SIZE + payload_length > _data->GetCapacity())
	{
		OV_ASSERT(false, "Data capacity must be greater than %ld (packet : %ld)",
				  _data->GetCapacity(), OVT_FIXED_HEADER_SIZE + payload_length);
		return false;
	}

	SetPayloadLength(payload_length);

	// OVT_DEFAULT_MAX_PACKET_SIZE
	//_data->SetLength(_payload_length);
	memcpy(&_buffer[OVT_FIXED_HEADER_SIZE], payload, payload_length);

	_is_packet_available = true;

	return true;
}
