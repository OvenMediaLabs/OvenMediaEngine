#include "file_stream.h"

#include <regex>

#include "base/publisher/application.h"
#include "base/publisher/stream.h"
#include "file_application.h"
#include "file_private.h"

namespace pub
{
	std::shared_ptr<FileStream> FileStream::Create(const std::shared_ptr<pub::Application> application,
												   const info::Stream &info)
	{
		auto stream = std::make_shared<FileStream>(application, info);
		return stream;
	}

	FileStream::FileStream(const std::shared_ptr<pub::Application> application,
						   const info::Stream &info)
		: Stream(application, info)
	{
	}

	FileStream::~FileStream()
	{
		logtt("FileStream(%s/%s) has been terminated finally",
			  GetApplicationName(), GetName().CStr());
	}

	bool FileStream::Start()
	{
		if (GetState() != Stream::State::CREATED)
		{
			return false;
		}

		logtt("FileStream(%u) has been started", GetId());

		if (!CreateStreamWorker(2))
		{
			return false;
		}

		_stream_interval_gate.Reset();

		auto result = Stream::Start();
		if (result == true)
		{
			std::static_pointer_cast<FileApplication>(GetApplication())->SessionUpdateByStream(std::static_pointer_cast<FileStream>(GetSharedPtr()), false);
		}

		return result;
	}

	bool FileStream::Stop()
	{
		if (GetState() != Stream::State::STARTED)
		{
			return false;
		}

		auto result = Stream::Stop();
		if (result == true)
		{
			std::static_pointer_cast<FileApplication>(GetApplication())->SessionUpdateByStream(std::static_pointer_cast<FileStream>(GetSharedPtr()), true);
		}

		return result;
	}

	void FileStream::SendFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		if (GetState() != Stream::State::STARTED)
		{
			return;
		}

		// Periodically update sessions that still need (re)starting
		if (_stream_interval_gate.TryConsume())
		{
			std::static_pointer_cast<FileApplication>(GetApplication())->SessionUpdateByStream(std::static_pointer_cast<FileStream>(GetSharedPtr()), false);
		}

		auto stream_packet = std::make_any<std::shared_ptr<MediaPacket>>(media_packet);

		BroadcastPacket(stream_packet);
	}

	void FileStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		SendFrame(media_packet);
	}

	void FileStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		SendFrame(media_packet);
	}

	std::shared_ptr<FileSession> FileStream::CreateSession()
	{
		auto session = FileSession::Create(GetApplication(), GetSharedPtrAs<pub::Stream>(), this->IssueUniqueSessionId());
		if (session == nullptr)
		{
			logte("Internal Error : Cannot create session");
			return nullptr;
		}

		AddSession(session);

		return session;
	}

	bool FileStream::DeleteSession(uint32_t session_id)
	{
		return RemoveSession(session_id);
	}
}  // namespace pub