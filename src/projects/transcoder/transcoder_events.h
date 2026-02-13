//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/application.h>

#include <stdint.h>
#include <memory>
#include <queue>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_type.h"
#include "transcoder_context.h"

class TranscoderEvents
{
public:
	TranscoderEvents();
	~TranscoderEvents();

public:
	bool PushEvent(std::shared_ptr<info::Stream> &stream, std::shared_ptr<MediaPacket> &packet);
	void RunEvent(std::shared_ptr<info::Stream> &stream, std::shared_ptr<MediaPacket> &packet);
	std::map<MediaTrackId, std::vector<std::shared_ptr<MediaPacket>>> _event_packets;

private:
	bool UpdateEventPacket(std::shared_ptr<MediaPacket> &event_packet);
};
