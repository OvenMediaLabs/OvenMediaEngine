#pragma once

#include <base/common_types.h>
#include <base/publisher/stream.h>
#include <modules/ovt_packetizer/ovt_packetizer.h>

#include "file_session.h"
#include "monitoring/monitoring.h"

namespace pub
{
	class FileStream final : public pub::Stream
	{
	public:
		static std::shared_ptr<FileStream> Create(const std::shared_ptr<pub::Application> application,
												  const info::Stream &info);

		explicit FileStream(const std::shared_ptr<pub::Application> application,
							const info::Stream &info);
		~FileStream() final;

		void SendFrame(const std::shared_ptr<MediaPacket> &media_packet);
		void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
		void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
		void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}  // Not supported

		std::shared_ptr<FileSession> CreateSession();
		bool DeleteSession(uint32_t session_id);

	private:
		bool Start() override;
		bool Stop() override;

		ov::StopWatch _stop_watch;
		std::shared_ptr<mon::StreamMetrics> _stream_metrics;
	};
}  // namespace pub