#include "rtsp_demuxer.h"

RtspDemuxer::RtspDemuxer()
{
	_buffer = std::make_shared<ov::Data>();
}

bool RtspDemuxer::AppendPacket(const std::shared_ptr<ov::Data> &packet)
{
	return AppendPacket(packet->GetDataAs<uint8_t>(), packet->GetLength());
}

bool RtspDemuxer::AppendPacket(const uint8_t *data, size_t data_length)
{
	_buffer->Append(data, data_length);

	while(_buffer->GetLength() > 0)
	{
		// Interleaved Binary data
		if(_buffer->At(0) == '$')
		{
			auto [rtsp_data, result] = RtspData::Parse(_buffer);
			// Success
			if(result > 0)
			{
				_datas.push(rtsp_data);
				_buffer = _buffer->Subdata(result);
				continue;
			}
			// Not enough buffer
			else if(result == 0)
			{
				break;
			}
			// Error
			else
			{
				return false;
			}
		}
		// Message
		else
		{
			auto [rtsp_message, result] = RtspMessage::Parse(_buffer);
			// Success
			if(result > 0)
			{
				_messages.push(rtsp_message);
				_buffer = _buffer->Subdata(result);
				continue;
			}
			// Not enough buffer
			else if(result == 0)
			{
				break;
			}
			// Error
			else
			{
				return false;
			}
		}
		
	}

	return true;
}

bool RtspDemuxer::IsAvailableMessage()
{
	return _messages.size() > 0;
}

bool RtspDemuxer::IsAvailableData()
{

	return _datas.size() > 0;
}

std::shared_ptr<RtspMessage> RtspDemuxer::PopMessage()
{
	if(IsAvailableMessage() == false)
	{
		return nullptr;
	}

	auto message = _messages.front();
	_messages.pop();

	return message;
}

std::shared_ptr<RtspData> RtspDemuxer::PopData()
{
	if(IsAvailableData() == false)
	{
		return nullptr;
	}
	
	auto data = _datas.front();
	_datas.pop();

	return data;
}