#pragma once

#include <base/common_types.h>
#include <base/publisher/stream.h>
#include <modules/ovt_packetizer/ovt_packetizer.h>

#include "monitoring/monitoring.h"

class ThumbnailStream final : public pub::Stream
{
public:
	static std::shared_ptr<ThumbnailStream> Create(const std::shared_ptr<pub::Application> application,
												   const info::Stream &info);

	explicit ThumbnailStream(const std::shared_ptr<pub::Application> application,
							 const info::Stream &info);
	~ThumbnailStream() final;

	void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override {} // Not supported

	std::shared_ptr<ov::Data> GetVideoFrameByCodecId(cmn::MediaCodecId codec_id, int64_t timeout_ms = 0);
private:
	bool Start() override;
	bool Stop() override;

	std::shared_mutex _encoded_frame_mutex;
	std::map<cmn::MediaCodecId, std::shared_ptr<ov::Data>> _encoded_frames;
	std::shared_ptr<mon::StreamMetrics> _stream_metrics;
};
