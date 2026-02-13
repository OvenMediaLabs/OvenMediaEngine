//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>

#include <memory>
#include <queue>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/mediarouter_application_connector.h"
#include "base/mediarouter/media_type.h"
#include "modules/managed_queue/managed_queue.h"

class MediaRouterNormalize
{
public:
	bool NormalizeMediaPacket(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessH264AVCCStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
	bool ProcessH264AnnexBStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
	bool InsertH264SPSPPSAnnexB(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet, bool need_aud = false);
	bool InsertH264AudAnnexB(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessH265AnnexBStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
	bool ProcessH265HVCCStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessAACRawStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
	bool ProcessAACAdtsStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessVP8Stream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessOPUSStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessMP3Stream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
};
