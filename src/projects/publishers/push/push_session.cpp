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
		logtt("PushSession(%d) has been terminated finally", GetId());
		MonitorInstance->OnSessionDisconnected(*GetStream(), PublisherType::Push);
	}

	bool PushSession::Start()
	{
		if (GetPush() == nullptr)
		{
			logte("Push object is null");
			SetState(SessionState::Error);
			return false;
		}

		GetPush()->UpdatePushStartTime();
		GetPush()->SetState(info::Push::PushState::Connecting);

		ov::String dest_url;
		if (GetPush()->GetStreamKey().IsEmpty())
		{
			dest_url = GetPush()->GetUrl();
		}
		else
		{
			dest_url = ov::String::FormatString("%s/%s", GetPush()->GetUrl().CStr(), GetPush()->GetStreamKey().CStr());
		}

		if (CreateWriter() == nullptr)
		{
			SetState(SessionState::Error);
			GetPush()->SetState(info::Push::PushState::Error);
			logte("Failed to create session. %s", _push->GetInfoString().CStr());
			return false;
		}

		if (GetWriter()->SetUrl(dest_url, ffmpeg::compat::GetFormatByProtocolType(GetPush()->GetProtocolType())) == false)
		{
			SetState(SessionState::Error);
			GetPush()->SetState(info::Push::PushState::Error);
			logte("Failed to set URL. Reason(%s), %s", GetWriter()->GetErrorMessage().CStr(), _push->GetInfoString().CStr());
			return false;
		}

		// RTMP, SRT, MPEG-TS Pushing uses different timestamp modes. default is zerobased.
		if (GetPush()->GetTimestampMode() == TimestampMode::Original)
		{
			GetWriter()->SetTimestampMode(ffmpeg::Writer::TIMESTAMP_PASSTHROUGH_MODE);
		}
		else
		{
			// Default TimestampMode is ZeroBased
			GetWriter()->SetTimestampMode(ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE);
		}

		// Set timeouts
		GetWriter()->SetConnectionTimeout(GetPush()->GetConnectionTimeout());
		GetWriter()->SetSendTimeout(GetPush()->GetSendTimeout());

		// Add Tracks
		if (GetPush()->GetTrackIds().empty() && GetPush()->GetVariantNames().empty())
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
			for (const auto &variant_name : GetPush()->GetVariantNames())
			{
				// VariantName format: "variantName:index", "variantName" (index is optional). if index is not specified, 0 is used.
				auto vars			  = variant_name.Split(":", 2);
				ov::String variant = vars[0];
				int32_t variant_index	  = (vars.size() >= 2) ? ov::Converter::ToInt32(vars[1], 0) : 0;

				// Find MediaTrack by VariantName and Index
				auto media_track	  = GetStream()->GetTrackByVariant(variant, variant_index);
				if (media_track == nullptr)
				{
					logtw("PushSession(%d) - Could not find track by VariantName: %s : %d", GetId(), variant.CStr(), variant_index);
					continue;
				}

				if (AddTrack(media_track) == false)
				{
					continue;
				}
			}

			// Select tracks by TrackIds
			for (const auto &track_id : GetPush()->GetTrackIds())
			{
				auto media_track = GetStream()->GetTrack(track_id);
				if (media_track == nullptr)
				{
					logtw("PushSession(%d) - Could not find track by TrackId: %d", GetId(), track_id);
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
					logtw("PushSession(%d) - Could not add data track. trackId:%d, variantName: %s", GetId(), data_track->GetId(), data_track->GetVariantName().CStr());
				}
			}
		}



		// Notice: If there are more than one video track, RTMP Push is not created and returns an error. You must use 1 video track.
		if (GetWriter()->Start() == false)
		{
			SetState(SessionState::Error);
			GetPush()->SetState(info::Push::PushState::Error);
			logte("Failed to start session. Reason(%s), %s", GetWriter()->GetErrorMessage().CStr(), GetPush()->GetInfoString().CStr());
			return false;
		}

		GetPush()->SetState(info::Push::PushState::Pushing);

		logtt("PushSession(%d) has started.", GetId());

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

			if (push != nullptr)
			{
				GetPush()->SetState(info::Push::PushState::Stopped);
				GetPush()->IncreaseSequence();
			}

			DestoryWriter();

			logtt("PushSession(%d) has stopped", GetId());
		}

		return Session::Stop();
	}

	void PushSession::SendOutgoingData(const std::any &packet)
	{
		if (GetState() != SessionState::Started)
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
			logtt("An incorrect type of packet was input from the stream. (%s)", e.what());

			return;
		}

		auto writer = GetWriter();
		if (writer == nullptr)
		{
			return;
		}

		uint64_t sent_bytes = 0;

		bool ret = writer->SendPacket(session_packet, &sent_bytes);
		if (ret == false)
		{
			logte("Failed to send packet. session will be terminated. Reason(%s), %s", writer->GetErrorMessage().CStr(), _push->GetInfoString().CStr());

			writer->Stop();

			SetState(SessionState::Error);
			GetPush()->SetState(info::Push::PushState::Error);

			return;
		}

		GetPush()->UpdatePushTime();
		GetPush()->IncreasePushBytes(sent_bytes);

		MonitorInstance->IncreaseBytesOut(*GetStream(), PublisherType::Push, sent_bytes);
	}

	std::shared_ptr<ffmpeg::Writer> PushSession::CreateWriter()
	{
		std::lock_guard<std::shared_mutex> lock(_writer_mutex);
		if (_writer != nullptr)
		{
			_writer->Stop();
			_writer = nullptr;
		}

		_writer = ffmpeg::Writer::Create();
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

		// Check already added
		if (writer->GetTrackByTrackId(track->GetId()) != nullptr)
		{
			logtw("Track already added. trackId:%d, variantName: %s", track->GetId(), track->GetVariantName().CStr());
			return false;
		}

		if (IsSupportTrack(GetPush()->GetProtocolType(), track) == false)
		{
			logtw("Could not supported track. trackId:%u, codecId: %d", track->GetId(), ov::ToUnderlyingType(track->GetCodecId()));
			return false;
		}

		if (IsSupportCodec(GetPush()->GetProtocolType(), track->GetCodecId()) == false)
		{
			logtw("Could not supported codec. trackId:%u, codecId: %d", track->GetId(), ov::ToUnderlyingType(track->GetCodecId()));
			return false;
		}

		// RTMP protocol only supports one track per media type.
		if (GetPush()->GetProtocolType() == info::Push::ProtocolType::RTMP)
		{
			if (GetWriter()->GetTrackCountByType(track->GetMediaType()) >= 1)
			{
				logtw("Could not add more than one video track for RTMP. trackId:%d, variantName: %s", track->GetId(), track->GetVariantName().CStr());
				return false;
			}
		}

		logtd("PushSession(%d) - Adding track. trackId:%d, variantName: %s", GetId(), track->GetId(), track->GetVariantName().CStr());

		return writer->AddTrack(track);
	}
}  // namespace pub