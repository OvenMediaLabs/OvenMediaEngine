#include "thumbnail_stream.h"

#include <regex>

#include "base/publisher/application.h"
#include "base/publisher/stream.h"
#include "thumbnail_private.h"

std::shared_ptr<ThumbnailStream> ThumbnailStream::Create(const std::shared_ptr<pub::Application> application,
														 const info::Stream &info)
{
	auto stream = std::make_shared<ThumbnailStream>(application, info);
	return stream;
}

ThumbnailStream::ThumbnailStream(const std::shared_ptr<pub::Application> application,
								 const info::Stream &info)
	: Stream(application, info)
{
}

ThumbnailStream::~ThumbnailStream()
{
	logtt("ThumbnailStream(%s/%s) has been terminated finally",
		  GetApplicationName(), GetName().CStr());
}

bool ThumbnailStream::Start()
{
	logtt("ThumbnailStream(%u) has been started", GetId());

	if (GetState() != Stream::State::CREATED)
	{
		return false;
	}

	// Check if there is a supported codec
	bool found = false;
	for (const auto &[id, track] : _tracks)
	{
		if ((track->GetCodecId() == cmn::MediaCodecId::Png ||
			 track->GetCodecId() == cmn::MediaCodecId::Jpeg ||
			 track->GetCodecId() == cmn::MediaCodecId::Webp))
		{
			found = true;
			break;
		}
	}

	if (found == false)
	{
		logtw("Stream [%s/%s] was not created because there were no supported codecs by the Thumbnail Publisher.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
		return false;
	}

	return Stream::Start();
}

bool ThumbnailStream::Stop()
{
	logtt("ThumbnailStream(%u) has been stopped", GetId());

	return Stream::Stop();
}

void ThumbnailStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	// if not started return
	if (GetState() != Stream::State::STARTED)
	{
		return;
	}
	
	auto track = GetTrack(media_packet->GetTrackId());
	if (track == nullptr)
	{
		logtw("Could not find track. id: %d", media_packet->GetTrackId());
		return;
	}

	if (!(track->GetCodecId() == cmn::MediaCodecId::Png ||
		  track->GetCodecId() == cmn::MediaCodecId::Jpeg ||
		  track->GetCodecId() == cmn::MediaCodecId::Webp))
	{
		// Could not support codec for image
		return;
	}

	std::lock_guard<std::shared_mutex> lock(_encoded_frame_mutex);
	if (media_packet->GetData() != nullptr)
	{
		_encoded_frames[track->GetCodecId()] = std::move(media_packet->GetData()->Clone());
	}
}

void ThumbnailStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	// Nothing..
}

std::shared_ptr<ov::Data> ThumbnailStream::GetVideoFrameByCodecId(cmn::MediaCodecId codec_id, int64_t timeout_ms)
{
	ov::StopWatch	watch;
	
	watch.Start();

	do
	{
		if (true)
		{
			std::shared_lock<std::shared_mutex> lock(_encoded_frame_mutex);
			auto it = _encoded_frames.find(codec_id);
			if (it != _encoded_frames.end())
			{
				return it->second;
			}
		}

		if( (timeout_ms > 0) && (watch.Elapsed() < timeout_ms))
		{
			usleep(100 * 1000);	 // 100ms
		}
		else
		{
			break;
		}

	} while (true);

	return nullptr;
}