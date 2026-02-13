//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mediarouter_alert.h"

#include <base/ovlibrary/ovlibrary.h>

#include "mediarouter_private.h"

using namespace cmn;

#define PTS_CORRECT_THRESHOLD_MS 3000

MediaRouterAlert::MediaRouterAlert()
{
	_alert_count_bframe = 0;
}

MediaRouterAlert::~MediaRouterAlert()
{
	_last_pts.clear();
	_last_pts_ms.clear();
}

void MediaRouterAlert::Init(const std::shared_ptr<info::Stream> &stream_info)
{
}

bool MediaRouterAlert::Update(
	const cmn::MediaRouterStreamType type,
	const bool prepared,
	const ov::ManagedQueue<std::shared_ptr<MediaPacket>> &packets_queue,
	const std::shared_ptr<info::Stream> &stream_info,
	const std::shared_ptr<MediaTrack> &media_track,
	const std::shared_ptr<MediaPacket> &media_packet)
{
	if (type == cmn::MediaRouterStreamType::OUTBOUND)
	{
		if (DetectInvalidPacketDuration(stream_info, media_track, media_packet) == false)
		{
			return false;
		}
	}

	if (type == cmn::MediaRouterStreamType::INBOUND)
	{
		if (DetectPTSDiscontinuity(stream_info, media_track, media_packet) == false)
		{
			return false;
		}
	}

	if (type == cmn::MediaRouterStreamType::INBOUND || type == cmn::MediaRouterStreamType::OUTBOUND)
	{
		if (DetectBBframes(stream_info, media_track, media_packet) == false)
		{
			return false;
		}
	}

	return true;
}

bool MediaRouterAlert::DetectInvalidPacketDuration(const std::shared_ptr<info::Stream> &stream_info, const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<MediaPacket> &media_packet)
{
	if (media_packet->GetDuration() < 0)
	{
		logte("[%s/%s(%u)] Detected abnormal packet duration. track:%u, duration: %lld",
			  stream_info->GetApplicationInfo().GetVHostAppName().CStr(),
			  stream_info->GetName().CStr(),
			  stream_info->GetId(),
			  media_track->GetId(),
			  media_packet->GetDuration());

		return false;
	}

	return true;
}

bool MediaRouterAlert::DetectBBframes(const std::shared_ptr<info::Stream> &stream_info, const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<MediaPacket> &media_packet)
{
	switch (media_packet->GetBitstreamFormat())
	{
		case cmn::BitstreamFormat::H264_ANNEXB:
		case cmn::BitstreamFormat::H264_AVCC:
		case cmn::BitstreamFormat::H265_ANNEXB:
		case cmn::BitstreamFormat::HVCC:
			if (_alert_count_bframe < 1)	// Reduced the number of warning log outputs from 10 to 1
			{
				if (media_track->GetTotalFrameCount() > 0 && _last_pts[media_track->GetId()] > media_packet->GetPts())
				{
					media_track->SetHasBframes(true);
				}

				// Display a warning message that b-frame exists
				if (media_track->HasBframes() == true)
				{
					logtw("[%s/%s(%u)] Detected a B-frame track. track:%u",
						  stream_info->GetApplicationInfo().GetVHostAppName().CStr(),
						  stream_info->GetName().CStr(),
						  stream_info->GetId(),
						  media_track->GetId());

					_alert_count_bframe++;
				}
			}
			break;
		default:
			break;
	}
	_last_pts[media_track->GetId()] = media_packet->GetPts();

	return true;
}

bool MediaRouterAlert::DetectPTSDiscontinuity(const std::shared_ptr<info::Stream> &stream_info, const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<MediaPacket> &media_packet)
{
	auto track_id = media_packet->GetTrackId();

	auto it = _last_pts_ms.find(track_id);
	if (it != _last_pts_ms.end())
	{
		int64_t pts_ms = media_packet->GetPts() * media_track->GetTimeBase().GetExpr();
		int64_t pts_diff_ms = pts_ms - _last_pts_ms[track_id];

		if (std::abs(pts_diff_ms) > PTS_CORRECT_THRESHOLD_MS)
		{
			if (IsVideoCodec(media_track->GetCodecId()) || IsAudioCodec(media_track->GetCodecId()))
			{
				logtw("[%s/%s(%u)] Detected discontinuity track. track:%u last.pts: %lld, cur.pts: %lld, diff: %lldms",
					  stream_info->GetApplicationInfo().GetVHostAppName().CStr(),
					  stream_info->GetName().CStr(),
					  stream_info->GetId(),
					  track_id,
					  _last_pts_ms[track_id],
					  pts_ms,
					  pts_diff_ms);
			}
		}

		// This value must be converted to milliseconds before storing, because the packet's PTS can vary depending on the timebase.
		_last_pts_ms[track_id] = pts_ms;
	}

	return true;
}