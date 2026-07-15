//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "push_session.h"

#include <base/info/stream.h>
#include <base/publisher/stream.h>

#include "push_private.h"

#define OV_LOG_PREFIX_FORMAT "[%s] (Id:%s) - "
#define OV_LOG_PREFIX_VALUE (GetStream() != nullptr ? GetStream()->GetUri().CStr() : "-"), \
							(_push != nullptr ? _push->GetId().CStr() : "-")

namespace pub
{
	std::shared_ptr<PushSession> PushSession::Create(const std::shared_ptr<pub::Application> &application,
													 const std::shared_ptr<pub::Stream> &stream,
													 uint32_t session_id,
													 std::shared_ptr<info::Push> &push)
	{
		auto session_info = info::Session(*std::static_pointer_cast<info::Stream>(stream), session_id);
		auto session = std::make_shared<PushSession>(session_info, application, stream, push);
		return session;
	}

	PushSession::PushSession(const info::Session &session_info,
							 const std::shared_ptr<pub::Application> &application,
							 const std::shared_ptr<pub::Stream> &stream,
							 const std::shared_ptr<info::Push> &push)
		: pub::Session(session_info, application, stream),
		  _push(push),
		  _writer(nullptr)
	{
		MonitorInstance->OnSessionConnected(*stream, PublisherType::Push);
	}

	PushSession::~PushSession()
	{
		Stop();
		logat("PushSession has been terminated finally");
		MonitorInstance->OnSessionDisconnected(*GetStream(), PublisherType::Push);
	}

	bool PushSession::Start()
	{
		auto push = GetPush();
		if (push == nullptr)
		{
			logae("Push object is null");
			SetState(SessionState::Error);
			return false;
		}

		push->UpdatePushStartTime();
		push->SetState(info::Push::PushState::Connecting);

		ov::String dest_url;
		if (push->GetStreamKey().IsEmpty())
		{
			dest_url = push->GetUrl();
		}
		else
		{
			dest_url = ov::String::FormatString("%s/%s", push->GetUrl().CStr(), push->GetStreamKey().CStr());
		}

		auto writer = CreateWriter();
		if (writer == nullptr)
		{
			SetErrorState(push);
			logae("Failed to create session. %s", push->GetInfoString().CStr());
			return false;
		}

		if (writer->SetUrl(dest_url, ffmpeg::compat::GetFormatByProtocolType(push->GetProtocolType())) == false)
		{
			SetErrorState(push);
			logae("Failed to set URL. Reason(%s), %s", writer->GetErrorMessage().CStr(), push->GetInfoString().CStr());
			return false;
		}

		// RTMP, SRT, MPEG-TS Pushing uses different timestamp modes. default is zerobased.
		if (push->GetTimestampMode() == TimestampMode::Original)
		{
			writer->SetTimestampMode(ffmpeg::Writer::TIMESTAMP_PASSTHROUGH_MODE);
		}
		else
		{
			// Default TimestampMode is ZeroBased
			writer->SetTimestampMode(ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE);
		}

		// Set timeouts
		writer->SetConnectionTimeout(push->GetConnectionTimeout());
		writer->SetSendTimeout(push->GetSendTimeout());

		// Add Tracks
		if (push->GetTrackIds().empty() && push->GetVariantNames().empty())
		{
			// If there is no specified track, add all tracks.
			for (auto &[track_id, media_track] : GetStream()->GetTracks())
			{
				if (AddTrack(media_track) == false)
				{
					continue;
				}
			}
		}
		else
		{
			// Select tracks by VariantNames
			for (const auto &variant_name : push->GetVariantNames())
			{
				// VariantName format: "variantName:index", "variantName" (index is optional). if index is not specified, 0 is used.
				auto vars			  = variant_name.Split(":", 2);
				ov::String variant = vars[0];
				int32_t variant_index	  = (vars.size() >= 2) ? ov::Converter::ToInt32(vars[1], 0) : 0;

				// Find MediaTrack by VariantName and Index
				auto media_track	  = GetStream()->GetTrackByVariant(variant, variant_index);
				if (media_track == nullptr)
				{
					logaw("Could not find track by VariantName: %s : %d", variant.CStr(), variant_index);
					continue;
				}

				if (AddTrack(media_track) == false)
				{
					continue;
				}
			}

			// Select tracks by TrackIds
			for (const auto &track_id : push->GetTrackIds())
			{
				auto media_track = GetStream()->GetTrack(track_id);
				if (media_track == nullptr)
				{
					logaw("Could not find track by TrackId: %d", track_id);
					continue;
				}

				if (AddTrack(media_track) == false)
				{
					continue;
				}
			}

			// Finally, add data track if exists.
			if (auto data_track = GetStream()->GetFirstTrackByType(cmn::MediaType::Data); data_track != nullptr)
			{
				if (AddTrack(data_track) == false)
				{
					logaw("Could not add data track. trackId:%d, variantName: %s", data_track->GetId(), data_track->GetVariantName().CStr());
				}
			}
		}



		// Notice: If there are more than one video track, RTMP Push is not created and returns an error. You must use 1 video track.
		if (writer->Start() == false)
		{
			SetErrorState(push);
			logae("Failed to start session. Reason(%s), %s", writer->GetErrorMessage().CStr(), push->GetInfoString().CStr());
			return false;
		}

		if (StartSenderThread() == false)
		{
			SetErrorState(push, writer);
			logae("Failed to start sender thread. %s", push->GetInfoString().CStr());
			return false;
		}

		push->SetState(info::Push::PushState::Pushing);

		logad("PushSession(%d) has started.", GetId());

		return Session::Start();
	}

	bool PushSession::Stop()
	{
		auto writer = GetWriter();
		if (writer != nullptr)
		{
			auto push = GetPush();
			if (push != nullptr)
			{
				push->SetState(info::Push::PushState::Stopping);
				push->UpdatePushStartTime();
			}

			writer->Stop();

			StopSenderThread();

			if (push != nullptr)
			{
				push->SetState(info::Push::PushState::Stopped);
				push->IncreaseSequence();
			}

			DestoryWriter();

			logad("PushSession(%d) has stopped", GetId());
		}

		return Session::Stop();
	}

	void PushSession::SendOutgoingData(const std::any &packet)
	{
		if (GetState() != SessionState::Started)
		{
			return;
		}

		if (_sender_stop_flag.load())
		{
			return;
		}

		std::shared_ptr<MediaPacket> session_packet;

		try
		{
			session_packet = std::any_cast<std::shared_ptr<MediaPacket>>(packet);
			if (session_packet == nullptr)
			{
				return;
			}
		}
		catch (const std::bad_any_cast &e)
		{
			logat("An incorrect type of packet was input from the stream. (%s)", e.what());

			return;
		}

		_sender_packet_queue.Enqueue(std::move(session_packet));
	}

	bool PushSession::StartSenderThread()
	{
		StopSenderThread();

		auto stream = GetStream();
		if (stream != nullptr)
		{
			auto urn = std::make_shared<info::ManagedQueue::URN>(
				stream->GetApplicationInfo().GetVHostAppName(),
				stream->GetName(),
				"pub",
				ov::String::FormatString("push_session_%u", GetId()).LowerCaseString());
			_sender_packet_queue.SetUrn(urn);
		}

		_sender_packet_queue.SetThreshold(100);
		_sender_packet_queue.Clear();

		_sender_stop_flag = false;
		_sender_thread = std::thread(&PushSession::SenderThread, this);
		pthread_setname_np(_sender_thread.native_handle(), ov::String::FormatString("PushSess-%u", GetId()).CStr());

		return true;
	}

	void PushSession::StopSenderThread()
	{
		_sender_stop_flag = true;
		
		_sender_packet_queue.InjectWakeup();

		if (_sender_thread.joinable())
		{
			_sender_thread.join();
		}

		_sender_packet_queue.Clear();
	}

	void PushSession::SenderThread()
	{
		ov::logger::ThreadHelper thread_helper;

		constexpr auto kThresholdGraceDuration = std::chrono::seconds(10);

		while (_sender_stop_flag.load() == false)
		{
			auto item = _sender_packet_queue.Dequeue(100);
			if (item.has_value() == false)
			{
				// Timeout or Stop is called. 
				continue;
			}

			auto session_packet = item.value();
			if (session_packet == nullptr)
			{
				continue;
			}

			auto writer = GetWriter();
			if (writer == nullptr)
			{
				continue;
			}

			auto push = GetPush();
			if (push == nullptr)
			{
				continue;
			}

			if (_sender_packet_queue.IsThresholdExceededFor(kThresholdGraceDuration))
			{
				logae("Push session queue exceeded state persisted for more than %ld seconds. Terminating session. %s",
					   std::chrono::duration_cast<std::chrono::seconds>(kThresholdGraceDuration).count(), push->GetInfoString().CStr());

				SetErrorState(push, writer);
				_sender_stop_flag = true;

				continue;
			}
			
			// Debug sleep to simulate slow network or processing. Remove this in production.
			// std::this_thread::sleep_for(std::chrono::milliseconds(ov::Random::GenerateUInt32(2, 15)));

			uint64_t sent_bytes = 0;
			bool ret = writer->SendPacket(session_packet, &sent_bytes);
			if (ret == false)
			{
				logae("Failed to send packet. session will be terminated. Reason(%s), %s", writer->GetErrorMessage().CStr(), push->GetInfoString().CStr());

				SetErrorState(push, writer);
				_sender_stop_flag = true;

				continue;
			}

			push->UpdatePushTime();
			push->IncreasePushBytes(sent_bytes);

			MonitorInstance->IncreaseBytesOut(*GetStream(), PublisherType::Push, sent_bytes);
		}
	}

	void PushSession::SetErrorState(const std::shared_ptr<info::Push> &push, const std::shared_ptr<ffmpeg::Writer> &writer)
	{
		if (writer != nullptr)
		{
			writer->Stop();
		}
		SetState(SessionState::Error);
		if (push != nullptr)
		{
			push->SetState(info::Push::PushState::Error);
		}
	}

	std::shared_ptr<ffmpeg::Writer> PushSession::CreateWriter()
	{
		std::lock_guard<std::shared_mutex> lock(_writer_mutex);
		if (_writer != nullptr)
		{
			_writer->Stop();
			_writer = nullptr;
		}

		_writer = ffmpeg::Writer::Create(ov::String::FormatString(OV_LOG_PREFIX_FORMAT, OV_LOG_PREFIX_VALUE));
		if (_writer == nullptr)
		{
			return nullptr;
		}

		return _writer;
	}

	std::shared_ptr<ffmpeg::Writer> PushSession::GetWriter()
	{
		std::shared_lock<std::shared_mutex> lock(_writer_mutex);
		return _writer;
	}

	void PushSession::DestoryWriter()
	{
		std::lock_guard<std::shared_mutex> lock(_writer_mutex);
		if (_writer != nullptr)
		{
			_writer->Stop();
			_writer = nullptr;
		}
	}

	std::shared_ptr<info::Push> PushSession::GetPush()
	{
		std::shared_lock<std::shared_mutex> lock(_push_mutex);
		return _push;
	}

	bool PushSession::IsSupportTrack(const info::Push::ProtocolType protocol_type, const std::shared_ptr<MediaTrack> &track)
	{
		if (protocol_type == info::Push::ProtocolType::RTMP)
		{
			if (track->GetMediaType() == cmn::MediaType::Video ||
				track->GetMediaType() == cmn::MediaType::Audio ||
				track->GetMediaType() == cmn::MediaType::Data)
			{
				return true;
			}
		}
		else if (protocol_type == info::Push::ProtocolType::SRT || protocol_type == info::Push::ProtocolType::MPEGTS)
		{
			if (track->GetMediaType() == cmn::MediaType::Video ||
				track->GetMediaType() == cmn::MediaType::Audio || 
				track->GetMediaType() == cmn::MediaType::Data)
			{
				return true;
			}
		}

		return false;
	}

	bool PushSession::IsSupportCodec(const info::Push::ProtocolType protocol_type, cmn::MediaCodecId codec_id)
	{
		// RTMP protocol does not supported except for H264 and AAC codec.
		if (protocol_type == info::Push::ProtocolType::RTMP)
		{
			if (codec_id == cmn::MediaCodecId::H264 ||
				codec_id == cmn::MediaCodecId::Aac ||
				codec_id == cmn::MediaCodecId::None)
			{
				return true;
			}
		}
		else if (protocol_type == info::Push::ProtocolType::SRT || protocol_type == info::Push::ProtocolType::MPEGTS)
		{
			if (codec_id == cmn::MediaCodecId::H264 ||
				codec_id == cmn::MediaCodecId::H265 ||
				codec_id == cmn::MediaCodecId::Vp8 ||
				codec_id == cmn::MediaCodecId::Vp9 ||
				codec_id == cmn::MediaCodecId::Aac ||
				codec_id == cmn::MediaCodecId::Mp3 ||
				codec_id == cmn::MediaCodecId::Opus || 
				codec_id == cmn::MediaCodecId::None)
			{
				return true;
			}
		}

		return false;
	}

	bool PushSession::AddTrack(const std::shared_ptr<MediaTrack> &track)
	{
		auto writer = GetWriter();
		if (writer == nullptr)
		{
			return false;
		}

		auto push = GetPush();
		if (push == nullptr)
		{
			return false;
		}

		// Check already added
		if (writer->GetTrackByTrackId(track->GetId()) != nullptr)
		{
			logaw("Track already added. trackId:%d, variantName: %s", track->GetId(), track->GetVariantName().CStr());
			return false;
		}

		if (IsSupportTrack(push->GetProtocolType(), track) == false)
		{
			logaw("Could not supported track. trackId:%u, codec: %s", track->GetId(), GetCodecIdString(track->GetCodecId()));
			return false;
		}

		if (IsSupportCodec(push->GetProtocolType(), track->GetCodecId()) == false)
		{
			logaw("Could not supported codec. trackId:%u, codec: %s", track->GetId(), GetCodecIdString(track->GetCodecId()));
			return false;
		}

		// RTMP protocol only supports one track per media type.
		if (push->GetProtocolType() == info::Push::ProtocolType::RTMP)
		{
			if (writer->GetTrackCountByType(track->GetMediaType()) >= 1)
			{
				logaw("Could not add more than one video track for RTMP. trackId:%d, variantName: %s", track->GetId(), track->GetVariantName().CStr());
				return false;
			}
		}

		logad("Adding track. trackId:%d, variantName: %s", track->GetId(), track->GetVariantName().CStr());

		return writer->AddTrack(track);
	}
}  // namespace pub
