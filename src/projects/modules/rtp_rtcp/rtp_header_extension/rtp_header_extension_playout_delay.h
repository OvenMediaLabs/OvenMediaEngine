#pragma once

// https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/playout-delay

#include <base/ovlibrary/ovlibrary.h>
#include "rtp_header_extension.h"

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID   | len=2 |       MIN delay       |       MAX delay       |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// a=extmap:12 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay

class RtpHeaderExtensionPlayoutDelay : public RtpHeaderExtension
{
public:
	RtpHeaderExtensionPlayoutDelay()
		: RtpHeaderExtensionPlayoutDelay(RTP_HEADER_EXTENSION_PLAYOUT_DELAY_ID)
	{	
	}

	RtpHeaderExtensionPlayoutDelay(uint8_t id)
		: RtpHeaderExtension(id)
	{
		_data = std::make_shared<ov::Data>(_buffer, sizeof(_buffer), true);
		memset(_buffer, 0, sizeof(_buffer));
	}

	void SetDelayMilliseconds(uint32_t min_delay_ms, uint32_t max_delay_ms)
	{
		// 12 bits for Minimum and Maximum delay. This represents a range of 0 - 40950 milliseconds for minimum and maximum (with a granularity of 10 ms). A granularity of 10 ms is sufficient since we expect the following typical use cases:
		if(min_delay_ms > 40950)
		{
			min_delay_ms = 40950;
		}

		if(max_delay_ms > 40950)
		{
			max_delay_ms = 40950;
		}

		SetDelay(min_delay_ms / 10, max_delay_ms / 10);
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
	void SetDelay(uint16_t min_delay, uint16_t max_delay)
	{
		// 12bit value
		if(min_delay > 0xFFF)
		{
			min_delay = 0xFFF;
		}

		if(max_delay > 0xFFF)
		{
			max_delay = 0xFFF;
		}

		_min_delay = min_delay;
		_max_delay = max_delay;

		// 50             | 250
		// 0000 0011 0010 | 0000 1111 1010
		// _buffer[0] = 0b00000011;
		// _buffer[1] = 0b00100000;
		// _buffer[2] = 0b11111010;

		_buffer[0] = (min_delay >> 4) & 0b11111111;
		_buffer[1] = (min_delay & 0b00001111) | ((max_delay >> 8) & 0b00001111);
		_buffer[2] = (max_delay & 0b11111111);
	}


	std::shared_ptr<ov::Data>	_data;
	uint8_t _buffer[3];

	uint16_t _min_delay = 0;
	uint16_t _max_delay = 0;
};