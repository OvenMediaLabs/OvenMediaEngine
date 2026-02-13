#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/ovlibrary/ovlibrary.h>

extern "C"
{
#include <libavformat/avformat.h>
};

namespace ffmpeg
{
	class Writer
	{
	public:
		enum TimestampMode
		{
			TIMESTAMP_STARTZERO_MODE = 0,
			TIMESTAMP_PASSTHROUGH_MODE = 1
		};

		enum WriterState
		{
			WriterStateNone = 0,
			WriterStateConnecting = 1,
			WriterStateConnected = 2,
			WriterStateClosing = 3,
			WriterStateClosed = 4,
			WriterStateError = 5
		};

		static const char* WriterStateToString(WriterState state)
		{
			switch (state)
			{
				case WriterStateNone: return "None";
				case WriterStateConnecting: return "Connecting";
				case WriterStateConnected: return "Connected";
				case WriterStateClosing: return "Closing";
				case WriterStateClosed: return "Closed";
				case WriterStateError: return "Error";					
				default: return "Unknown";
			}
		}
		
	public:
		static std::shared_ptr<Writer> Create();

		Writer();
		~Writer();

		static int InterruptCallback(void *opaque);

		// format is muxer(or container)
		// 	- RTMP : flv
		// 	- MPEGTS : mpegts, mp4
		//  - SRT : mpegts
		bool SetUrl(const ov::String url, const ov::String format = nullptr);
		ov::String GetUrl();

		bool Start();
		bool Stop();

		void SetConnectionTimeout(int32_t timeout_ms);
		int32_t GetConnectionTimeout() const;
		
		void SetSendTimeout(int32_t timeout_ms);
		int32_t GetSendTimeout() const;
				
		bool AddTrack(const std::shared_ptr<MediaTrack> &media_track);
		bool SendPacket(const std::shared_ptr<MediaPacket> &packet, uint64_t *sent_bytes = nullptr);
		
		int32_t GetTrackCountByType(cmn::MediaType media_type);
		std::shared_ptr<MediaTrack> GetTrackByTrackId(int32_t track_id) const;
				
		std::chrono::high_resolution_clock::time_point GetLastPacketSentTime();

		void SetTimestampMode(TimestampMode mode);
		TimestampMode GetTimestampMode();

		void SetState(WriterState state);
		WriterState GetState();
		
		void SetErrorMessage(const ov::String &message);
		ov::String GetErrorMessage() const;

	private:
		std::shared_ptr<AVFormatContext> GetAVFormatContext() const;
		void SetAVFormatContext(AVFormatContext* av_format);
		void ReleaseAVFormatContext();
		std::pair<std::shared_ptr<AVStream>, std::shared_ptr<MediaTrack>> GetTrack(int32_t track_id, cmn::BitstreamFormat format) const;
		bool ToAVPacket(AVPacket &av_packet, const std::shared_ptr<AVStream> av_stream, const std::shared_ptr<MediaPacket> &media_packet, const std::shared_ptr<MediaTrack> &media_track, int64_t start_time);
		std::shared_ptr<AVStream> CreateAVStream(const std::shared_ptr<MediaTrack> &media_track);

		std::atomic<WriterState> _state;

		ov::String _url;
		ov::String _format;

		int64_t _start_time = -1LL;
		TimestampMode _timestamp_mode = TIMESTAMP_STARTZERO_MODE;

		bool _need_to_flush = false;
		bool _need_to_close = false;

		// MediaTrackId -> AVStream, MediaTrack
		bool AddMediaTrack(const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<AVStream> &av_stream);
		bool AddEventTrack(const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<AVStream> &av_stream, cmn::BitstreamFormat format);
		std::map<int32_t, std::pair<std::shared_ptr<AVStream>, std::shared_ptr<MediaTrack>>> _av_track_map;
		std::map<std::pair<int32_t, cmn::BitstreamFormat>, std::pair<std::shared_ptr<AVStream>, std::shared_ptr<MediaTrack>>> _event_track_map;
		mutable std::shared_mutex _track_map_lock;

		std::shared_ptr<AVFormatContext> _av_format = nullptr;
		mutable std::shared_mutex _av_format_lock;

		ov::String _output_format_name;
		AVIOInterruptCB _interrupt_cb;
		std::chrono::high_resolution_clock::time_point _last_packet_sent_time;
		int32_t _connection_timeout = 5000;	// Default 5s
		int32_t _send_timeout 		= 2000;	// Default 2s

		// For tracking last DTS per track to handle DTS monotonicity
		std::map<int32_t, int64_t> _track_last_dts_map;

		ov::String _error_message;
	};
}  // namespace ffmpeg