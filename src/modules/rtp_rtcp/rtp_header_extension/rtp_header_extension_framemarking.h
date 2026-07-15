#pragma once

// https://tools.ietf.org/id/draft-ietf-avtext-framemarking-09.html

#include <base/ovlibrary/ovlibrary.h>
#include "rtp_header_extension.h"

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID=? |  L=2  |S|E|I|D|B| TID |   LID         |    TL0PICIDX  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//            or
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID=? |  L=1  |S|E|I|D|B| TID |   LID         | (TL0PICIDX omitted)
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//            or
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID=? |  L=0  |S|E|I|D|B| TID | (LID and TL0PICIDX omitted)
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// OvenMediaEnigne doesn't use LID and TL0PICIDX

class RtpHeaderExtensionFrameMarking : public RtpHeaderExtension
{
public:
	RtpHeaderExtensionFrameMarking()
		: RtpHeaderExtensionFrameMarking(RTP_HEADER_EXTENSION_FRAMEMARKING_ID)
	{	
	}

	RtpHeaderExtensionFrameMarking(uint8_t id)
		: RtpHeaderExtension(id)
	{
		_data = std::make_shared<ov::Data>(_buffer, sizeof(_buffer), true);
		memset(_buffer, 0, sizeof(_buffer));
	}

	void Reset()
	{
		_start_of_frame = false;
		_end_of_frame = false;
		_independent_frame = false;
		_discardable_frame = false;
		_base_layer_sync = false;
		_temporal_id = 0;	

		memset(_buffer, 0, sizeof(_buffer));

		UpdateData();
	}
	
	void SetStartOfFrame()
	{
		_buffer[0] |= 0x80;
		_start_of_frame = true;

		UpdateData();
	}

	void SetEndOfFrame()
	{
		_buffer[0] |= 0x40;
		_end_of_frame = true;

		UpdateData();
	}

	void SetIndependentFrame()
	{
		_buffer[0] |= 0x20;
		_independent_frame = true;

		UpdateData();
	}

	void SetDiscardableFrame()
	{
		_buffer[0] |= 0x10;
		_discardable_frame = true;

		UpdateData();
	}

	void SetBaseLayerSync()
	{
		_buffer[0] |= 0x08;
		_base_layer_sync = true;

		UpdateData();
	}

	void SetTemporalId(uint8_t id)
	{
		if(id > 0x7)
		{
			return;
		}

		_buffer[0] |= id;
		_temporal_id = id;

		UpdateData();
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
	uint8_t _buffer[1];

	bool _start_of_frame = false;
	bool _end_of_frame = false;
	bool _independent_frame = false;
	bool _discardable_frame = false;
	bool _base_layer_sync = false;
	uint8_t _temporal_id = 0;

	// Not used
	[[maybe_unused]] uint8_t _layer_id = 0;
	[[maybe_unused]] uint8_t _tl0picidx = 0;
};