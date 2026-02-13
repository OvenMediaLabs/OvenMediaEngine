#pragma once

// http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time

#include <base/ovlibrary/ovlibrary.h>
#include "rtp_header_extension.h"

//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |  ID   | len=2 |              absolute send time               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// a=extmap:4 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time

class RtpHeaderExtensionAbsSendTime : public RtpHeaderExtension
{
public:
	RtpHeaderExtensionAbsSendTime()
		: RtpHeaderExtensionAbsSendTime(RTP_HEADER_EXTENSION_ABS_SEND_TIME_ID)
	{	
	}

	RtpHeaderExtensionAbsSendTime(uint8_t id)
		: RtpHeaderExtension(id)
	{
		_data = std::make_shared<ov::Data>(_buffer, sizeof(_buffer), true);
		memset(_buffer, 0, sizeof(_buffer));
	}

	void SetAbsSendTime(uint32_t abs_send_time)
	{
		_abs_send_time = abs_send_time;
		ByteWriter<uint24_t>::WriteBigEndian(&_buffer[0], _abs_send_time);
		UpdateData();
	}

	void SetAbsSendTimeMs(int64_t time_ms)
	{
		auto abs_send_time = static_cast<uint32_t>(((time_ms << 18) + 500) / 1000) & 0x00ffffff;
		SetAbsSendTime(abs_send_time);
	}

	static uint32_t MsToAbsSendTime(int64_t time_ms)
	{
		auto abs_send_time = static_cast<uint32_t>(((time_ms << 18) + 500) / 1000) & 0x00ffffff;
		return abs_send_time;
	}

	bool SetData(const std::shared_ptr<ov::Data> &data) override
	{
		//TODO(Getroot): Parsing
		_data = data;

		return true;
	}

protected:
	std::shared_ptr<ov::Data> GetData(HeaderType type) override
	{
		return _data;
	}

private:

	std::shared_ptr<ov::Data>	_data;
	uint8_t _buffer[3];

	uint32_t _abs_send_time = 0;
};