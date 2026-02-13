//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "push_stream.h"

#include <regex>

#include "base/publisher/application.h"
#include "base/publisher/stream.h"
#include "push_application.h"
#include "push_private.h"

namespace pub
{
	std::shared_ptr<PushStream> PushStream::Create(const std::shared_ptr<pub::Application> application,
														   const info::Stream &info)
	{
		auto stream = std::make_shared<PushStream>(application, info);
		return stream;
	}

	PushStream::PushStream(const std::shared_ptr<pub::Application> application,
								   const info::Stream &info)
		: Stream(application, info)
	{
	}

	PushStream::~PushStream()
	{
		logtt("PushStream(%s/%s) has been terminated finally",
			  GetApplicationName(), GetName().CStr());
	}

	bool PushStream::Start()
	{
		if (GetState() != Stream::State::CREATED)
		{
			return false;
		}

		if (!CreateStreamWorker(2))
		{
			return false;
		}

		logtt("PushStream(%u) has been started", GetId());

		return Stream::Start();
	}

	bool PushStream::Stop()
	{
		logtt("PushStream(%u) has been stopped", GetId());

		if (GetState() != Stream::State::STARTED)
		{
			return false;
		}

		return Stream::Stop();
	}

	void PushStream::SendFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		if (GetState() != Stream::State::STARTED)
		{
			return;
		}

		auto stream_packet = std::make_any<std::shared_ptr<MediaPacket>>(media_packet);

		BroadcastPacket(stream_packet);
	}

	void PushStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		SendFrame(media_packet);
	}

	void PushStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		SendFrame(media_packet);
	}

	void PushStream::SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		SendFrame(media_packet);
	}

	std::shared_ptr<pub::Session> PushStream::CreatePushSession(std::shared_ptr<info::Push> &push)
	{
		auto session = std::static_pointer_cast<pub::Session>(PushSession::Create(GetApplication(), GetSharedPtrAs<pub::Stream>(), this->IssueUniqueSessionId(), push));
		if (session == nullptr)
		{
			logte("Internal Error : Cannot create session");
			return nullptr;
		}

		AddSession(session);

		return session;
	}
}  // namespace pub