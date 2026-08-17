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

// $ + ID + Length = 4
#define RTSP_INTERLEAVED_DATA_HEADER_LEN	4	
class RtspData : public ov::Data
{
public:
	using ov::Data::Data;

	// If success, returns RtspData and parsed data length
	// If not enough data, returns nullptr and 0
	// If fail, returns nullptr and -1
	// Usage : auto [message, parsed_bytes] = RtspData::Parse(~);
	static std::tuple<std::shared_ptr<RtspData>, int> Parse(const std::shared_ptr<const ov::Data> &data);

	// Only use in Parse()
	RtspData(){}
	RtspData(uint8_t channel_id, const std::shared_ptr<ov::Data> &data);

	uint8_t GetChannelId() const;

private:
	int ParseInternal(const std::shared_ptr<const ov::Data> &data);

	uint8_t _channel_id;
};