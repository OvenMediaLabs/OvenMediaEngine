#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovlibrary/byte_io.h>

// https://www.rfc-editor.org/rfc/rfc8656.html#name-the-channeldata-message

#define FIXED_TURN_CHANNEL_HEADER_SIZE	4

class ChannelDataMessage
{
public:
	enum LastErrorCode : uint8_t
	{
		NOT_USED = 0,
		SUCCESS,
		INVALID_DATA,
		NOT_ENOUGH_DATA
	};

	ChannelDataMessage()
	{
	}

	ChannelDataMessage(uint16_t channel_number, const std::shared_ptr<const ov::Data> &data)
	{
		SetPacket(channel_number, data);
	}

	bool SetPacket(uint16_t channel_number, const std::shared_ptr<const ov::Data> &data)
	{
		_channel_number = channel_number;
		
		_data_length = data->GetLength();

		// https://www.rfc-editor.org/rfc/rfc8656.html#section-12.5
		// The padding is not reflected in the length field of the ChannelData message, so the actual size of a ChannelData message (including padding) is (4 + Length) rounded up to the nearest multiple of 4 (see Section 14 of [RFC8489]). Over UDP, the padding is not required but MAY be included.
		uint16_t padding_length = 0;
		if(_data_length % 4 != 0)
		{
			padding_length = (((_data_length / 4) + 1) * 4) - _data_length;
		}

		_packet_length = FIXED_TURN_CHANNEL_HEADER_SIZE + _data_length + padding_length;

		_data = data->Clone();

		auto packet_buffer = std::make_shared<ov::Data>();
		packet_buffer->SetLength(_packet_length);

		auto p = packet_buffer->GetWritableDataAs<uint8_t>();

		memset(p, 0, packet_buffer->GetLength());
		ByteWriter<uint16_t>::WriteBigEndian(&p[0], _channel_number);
		ByteWriter<uint16_t>::WriteBigEndian(&p[2], _data_length);
		memcpy(&p[4], data->GetDataAs<uint8_t>(), _data_length);

		_packet_buffer = packet_buffer->Clone();

		return true;
	}

	bool LoadHeader(const ov::Data &packet)
	{
		if(packet.GetLength() < FIXED_TURN_CHANNEL_HEADER_SIZE)
		{
			_last_error_code = LastErrorCode::NOT_ENOUGH_DATA;
			return false;
		}

		auto buffer = packet.GetDataAs<uint8_t>();
		_channel_number = ByteReader<uint16_t>::ReadBigEndian(&buffer[0]);
		_data_length = ByteReader<uint16_t>::ReadBigEndian(&buffer[2]);

		uint16_t padding_length = 0;
		if(_data_length % 4 != 0)
		{
			padding_length = (((_data_length / 4) + 1) * 4) - _data_length;
		}

		_packet_length = FIXED_TURN_CHANNEL_HEADER_SIZE + _data_length + padding_length;

		if(packet.GetLength() < static_cast<size_t>(_packet_length))
		{
			_last_error_code = LastErrorCode::NOT_ENOUGH_DATA;
			return false;
		}

		return true;
	}

	bool Load(const std::shared_ptr<const ov::Data> &packet)
	{
		if(LoadHeader(*packet) == false)
		{
			return false;
		}

		_packet_buffer = packet;
		_data = _packet_buffer->Subdata(FIXED_TURN_CHANNEL_HEADER_SIZE, _data_length);

		_last_error_code = SUCCESS;

		return true;
	}

	bool Load(const ov::Data &packet)
	{
		if(LoadHeader(packet) == false)
		{
			return false;
		}

		_packet_buffer = packet.Clone();
		_data = _packet_buffer->Subdata(FIXED_TURN_CHANNEL_HEADER_SIZE, _data_length);

		_last_error_code = SUCCESS;
		return true;
	}

	LastErrorCode GetLastErrorCode()
	{
		return _last_error_code;
	}

	uint16_t GetPacketLength()
	{
		return _packet_length;
	}

	uint16_t GetChannelNumber()
	{
		return _channel_number;
	}
	uint16_t GetDataLength()
	{
		return _data_length;
	}
	
	std::shared_ptr<const ov::Data> GetPacket()
	{
		return _packet_buffer;
	}

	std::shared_ptr<const ov::Data> GetData()
	{
		return _data;
	}

private:
	uint16_t	_channel_number = 0;
	uint16_t	_data_length = 0;
	uint16_t	_packet_length = 0; // FIXED_HEADER + _data_length + padding () (multiple of 4)
	std::shared_ptr<const ov::Data>	_data = nullptr;
	std::shared_ptr<const ov::Data>	_packet_buffer = nullptr;

	LastErrorCode _last_error_code = LastErrorCode::NOT_USED;
};