//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/info/media_track.h>
#include <base/publisher/session.h>
#include <modules/ffmpeg/writer.h>
#include <modules/ffmpeg/compat.h>

#include "base/info/push.h"

namespace pub
{
	class PushSession : public pub::Session
	{
	public:
		static std::shared_ptr<PushSession> Create(const std::shared_ptr<pub::Application> &application,
													   const std::shared_ptr<pub::Stream> &stream,
													   uint32_t ovt_session_id,
													   std::shared_ptr<info::Push> &push);

		PushSession(const info::Session &session_info,
						const std::shared_ptr<pub::Application> &application,
						const std::shared_ptr<pub::Stream> &stream,
						const std::shared_ptr<info::Push> &push);
		~PushSession() override;

		bool Start() override;
		bool Stop() override;

		void SendOutgoingData(const std::any &packet) override;

		std::shared_ptr<info::Push> GetPush();
		std::shared_ptr<ffmpeg::Writer> GetWriter();

	private:
		std::shared_ptr<ffmpeg::Writer> CreateWriter();
		void DestoryWriter();

		bool AddTrack(const std::shared_ptr<MediaTrack> &track);
		bool IsSupportTrack(const info::Push::ProtocolType protocol_type, const std::shared_ptr<MediaTrack> &track);
		bool IsSupportCodec(const info::Push::ProtocolType protocol_type, cmn::MediaCodecId codec_id);

		std::shared_ptr<info::Push> _push = nullptr;
		std::shared_mutex _push_mutex;		

		std::shared_ptr<ffmpeg::Writer> _writer = nullptr;
		std::shared_mutex _writer_mutex;
	};
}  // namespace pub