#pragma once

#include <base/info/media_track.h>
#include <base/publisher/session.h>
#include <modules/ffmpeg/writer.h>

#include "base/info/record.h"

namespace pub
{
	class FileSession : public pub::Session
	{
	public:
		static std::shared_ptr<FileSession> Create(const std::shared_ptr<pub::Application> &application,
												   const std::shared_ptr<pub::Stream> &stream,
												   uint32_t ovt_session_id);

		FileSession(const info::Session &session_info,
					const std::shared_ptr<pub::Application> &application,
					const std::shared_ptr<pub::Stream> &stream);
		~FileSession() override;

		bool Start() override;
		bool Stop() override;

		bool Split();
		bool StartRecord();
		bool StopRecord();

		void SendOutgoingData(const std::any &packet) override;

		void SetRecord(std::shared_ptr<info::Record> &record);
		std::shared_ptr<info::Record> &GetRecord();
		
		bool AddTrack(const ov::String output_format, const std::shared_ptr<MediaTrack> &track);
		
	private:
		ov::String GetRootPath();
		ov::String GetOutputTempFilePath(std::shared_ptr<info::Record> &record);
		ov::String GetOutputFilePath();
		ov::String GetOutputFileInfoPath();

		void SelectDefaultTrack(const std::shared_ptr<MediaTrack> &track);
		bool IsSupportCodec(const ov::String output_format, const cmn::MediaCodecId codec_id);
		std::shared_ptr<ffmpeg::Writer> CreateWriter();
		std::shared_ptr<ffmpeg::Writer> GetWriter();
		void DestroyWriter();

	private:
		std::shared_ptr<ffmpeg::Writer> _writer;
		std::shared_mutex _writer_mutex;

		std::shared_ptr<info::Record> _record;
		std::shared_mutex _record_mutex;

		std::map<cmn::MediaType, MediaTrackId> _default_track_by_type;
		MediaTrackId _default_track;
		bool _found_first_keyframe = false;

		std::atomic<bool> _is_splitting = false;
	};
}  // namespace pub