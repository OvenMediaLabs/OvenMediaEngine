#include "rtsp_data.h"
#include <base/ovlibrary/byte_io.h>

std::tuple<std::shared_ptr<RtspData>, int> RtspData::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if(data->GetLength() < 4)
	{
		// not enough data
		return {nullptr, 0};
	}

	if(data->GetDataAs<uint8_t>()[0] != '$')
	{
		return {nullptr, -1};
	}

	auto rtsp_data = std::make_shared<RtspData>();
	auto parsed_length = rtsp_data->ParseInternal(data);

	return {parsed_length > 0? rtsp_data : nullptr, parsed_length};
}

int RtspData::ParseInternal(const std::shared_ptr<const ov::Data> &data)
{	
	// ${1 bytes Channel ID}{2 bytes Length}{Length bytes data}
	// S->C: $\000{2 byte length}{"length" bytes data, w/RTP header}
	// S->C: $\000{2 byte length}{"length" bytes data, w/RTP header}
	// S->C: $\001{2 byte length}{"length" bytes  RTCP packet}

	if(data->GetLength() < RTSP_INTERLEAVED_DATA_HEADER_LEN)
	{
		// not enough data
		return 0;
	}

	auto ptr = data->GetDataAs<uint8_t>();

	if(ptr[0] != '$')
	{
		// error
		return -1;
	}

	_channel_id = ByteReader<uint8_t>::ReadBigEndian(&ptr[1]);
	auto data_length = ByteReader<uint16_t>::ReadBigEndian(&ptr[2]);

	if(static_cast<size_t>(RTSP_INTERLEAVED_DATA_HEADER_LEN + data_length) > data->GetLength())
	{
		// not enough data
		return 0;
	}

	Clear();
	Append(&ptr[4], data_length);

	return RTSP_INTERLEAVED_DATA_HEADER_LEN + data_length;;
}

RtspData::RtspData(uint8_t channel_id, const std::shared_ptr<ov::Data> &data)
	: RtspData(data->GetData(), data->GetLength())
{
	_channel_id = channel_id;
}

uint8_t RtspData::GetChannelId() const
{
	return _channel_id;
}