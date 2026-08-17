//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "srt_session.h"

#include <base/info/stream.h>
#include <base/ovlibrary/byte_io.h>
#include <base/publisher/stream.h>
#include <monitoring/monitoring.h>

#include "srt_application.h"
#include "srt_private.h"
#include "srt_stream.h"

#define OV_LOG_PREFIX_FORMAT "[%s#%u] "
#define OV_LOG_PREFIX_VALUE GetAppStreamName().CStr(), GetId()

namespace pub
{
	std::shared_ptr<SrtSession> SrtSession::Create(const std::shared_ptr<Application> &application,
												   const std::shared_ptr<Stream> &stream,
												   uint32_t session_id,
												   const std::shared_ptr<ov::Socket> &connector,
												   const std::shared_ptr<SrtPlaylist> &srt_playlist)
	{
		auto session_info = info::Session(*std::static_pointer_cast<info::Stream>(stream), session_id);
		auto session = std::make_shared<SrtSession>(session_info, application, stream, connector, srt_playlist);

		if (session->Start() == false)
		{
			return nullptr;
		}

		return session;
	}

	SrtSession::SrtSession(const info::Session &session_info,
						   const std::shared_ptr<Application> &application,
						   const std::shared_ptr<Stream> &stream,
						   const std::shared_ptr<ov::Socket> &connector,
						   const std::shared_ptr<SrtPlaylist> &srt_playlist)
		: Session(session_info, application, stream)
	{
		_connector = connector;
		_srt_playlist = srt_playlist;

		MonitorInstance->OnSessionConnected(*GetStream(), PublisherType::Srt);
	}

	SrtSession::~SrtSession()
	{
		Stop();

		logat("SrtSession has finally been terminated");

		MonitorInstance->OnSessionDisconnected(*GetStream(), PublisherType::Srt);
	}

	ov::String SrtSession::GetAppStreamName() const
	{
		const auto &application = GetApplication();
		const auto &stream = GetStream();

		return ov::String::FormatString(
			"%s/%s(%u)",
			application->GetVHostAppName().CStr(),
			stream->GetName().CStr(),
			stream->GetId());
	}

	bool SrtSession::Start()
	{
		logat("SrtSession will be started");
		return Session::Start();
	}

	bool SrtSession::Stop()
	{
		logat("SrtSession has stopped");
		_connector->Close();
		return Session::Stop();
	}

	std::shared_ptr<const SrtData> SrtSession::ToSrtData(const std::any &packet)
	{
		std::shared_ptr<const SrtData> srt_data;

		try
		{
			return std::any_cast<std::shared_ptr<const SrtData>>(packet);
		}
		catch (const std::bad_any_cast &e)
		{
			logat("An incorrect type of packet was input from the stream. (%s)", e.what());

			OV_ASSERT2(false);
			return nullptr;
		}
	}

	void SrtSession::SendOutgoingData(const std::any &packet)
	{
		auto srt_data = ToSrtData(packet);

		if (srt_data == nullptr)
		{
			// The packet is not for this session
			return;
		}

		auto playlist = srt_data->playlist.lock();

		if (
			// The playlist has been destroyed
			(playlist == nullptr) ||
			(playlist != _srt_playlist))
		{
			return;
		}

		std::shared_ptr<const ov::Data> mpegts_data;

		if (_need_to_send_psi)
		{
			_need_to_send_psi = false;
			_connector->Send(playlist->GetPsiData());
		}

		mpegts_data = srt_data->data;

		if (mpegts_data == nullptr)
		{
			OV_ASSERT2("SrtStream::BroadcastPacket never sends a null packet");
			return;
		}

		_connector->Send(mpegts_data);
	}

	const std::shared_ptr<ov::Socket> SrtSession::GetConnector()
	{
		return _connector;
	}

	void SrtSession::OnMessageReceived(const std::any &message)
	{
		// Client does not send any message
		OV_ASSERT2(false);
	}
}  // namespace pub
