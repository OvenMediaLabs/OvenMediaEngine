//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "rtsp_message.h"
#include "rtsp_data.h"

class RtspDemuxer
{
public:
	RtspDemuxer();
	bool AppendPacket(const std::shared_ptr<ov::Data> &packet);
	bool AppendPacket(const uint8_t *data, size_t data_length);

	bool IsAvailableMessage();
	bool IsAvailableData();

	std::shared_ptr<RtspMessage> PopMessage();
	std::shared_ptr<RtspData> PopData();

private:
	std::shared_ptr<ov::Data> _buffer;

	std::queue<std::shared_ptr<RtspMessage>> _messages;
	// Interleaved binary data
	std::queue<std::shared_ptr<RtspData>> _datas;
};