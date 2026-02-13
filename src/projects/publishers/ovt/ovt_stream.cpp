

#include "ovt_private.h"
#include "ovt_stream.h"
#include "ovt_session.h"
#include "base/publisher/application.h"
#include "base/publisher/stream.h"

#include <modules/ovt_packetizer/ovt_signaling.h>
#include <orchestrator/orchestrator.h>

std::shared_ptr<OvtStream> OvtStream::Create(const std::shared_ptr<pub::Application> application,
											 const info::Stream &info,
											 uint32_t worker_count)
{
	auto stream = std::make_shared<OvtStream>(application, info, worker_count);
	return stream;
}

OvtStream::OvtStream(const std::shared_ptr<pub::Application> application,
					 const info::Stream &info,
					 uint32_t worker_count)
		: Stream(application, info),
		_worker_count(worker_count)
{
	logtt("OvtStream(%s/%s) has been started", GetApplicationName() , GetName().CStr());
}

OvtStream::~OvtStream()
{
	logtt("OvtStream(%s/%s) has been terminated finally", GetApplicationName() , GetName().CStr());
}

bool OvtStream::Start()
{
	if(GetState() != Stream::State::CREATED)
	{
		return false;
	}

	// If this stream is from OriginMapStore, don't register it to OriginMapStore again.
	if (IsFromOriginMapStore() == false)
	{
		auto result = ocst::Orchestrator::GetInstance()->RegisterStreamToOriginMapStore(GetApplicationInfo().GetVHostAppName(), GetName());
		if (result == CommonErrorCode::ERROR)
		{
			logtw("Failed to register stream to origin map store : %s/%s", GetApplicationName(), GetName().CStr());
		}
	}

	if(!CreateStreamWorker(_worker_count))
	{
		return false;
	}

	logtt("OvtStream(%d) has been started", GetId());
	_packetizer = std::make_shared<OvtPacketizer>(OvtPacketizerInterface::GetSharedPtr());

	return Stream::Start();
}

bool OvtStream::Stop()
{
	if(GetState() != Stream::State::STARTED)
	{
		return false;
	}

	logtt("OvtStream(%u) has been stopped", GetId());

	if (GetLinkedInputStream() != nullptr && GetLinkedInputStream()->IsFromOriginMapStore() == false)
	{
		// Unegister stream if OriginMapStore is enabled
		auto result = ocst::Orchestrator::GetInstance()->UnregisterStreamFromOriginMapStore(GetApplicationInfo().GetVHostAppName(), GetName());
		if (result == CommonErrorCode::ERROR)
		{
			logtw("Failed to unregister stream from origin map store : %s/%s", GetApplicationName(), GetName().CStr());
			return false;
		}
	}

	std::unique_lock<std::shared_mutex> mlock(_packetizer_lock);
	if(_packetizer != nullptr)
	{	
		_packetizer->Release();
		_packetizer.reset();
	}
	mlock.unlock();

	return Stream::Stop();
}

bool OvtStream::GenerateDescription()
{
	Json::Value 	json_root;
	Json::Value		json_stream;
	Json::Value		json_tracks;
	Json::Value		json_playlists;

	json_root["version"] = OVT_SIGNALING_VERSION;
	
	json_stream["appName"] = GetApplicationName();
	json_stream["streamName"] = GetName().CStr();

	// Since the OVT publisher is also an output stream, it transmits the UUID of the input stream.
	if (GetLinkedInputStream() != nullptr)
	{
		json_stream["originStreamUUID"] = GetLinkedInputStream()->GetUUID().CStr();
	}
	else
	{
		json_stream["originStreamUUID"] = GetUUID().CStr();
	}

	for(const auto &[file_name, playlist] : GetPlaylists())
	{
		Json::Value json_playlist;
		
		json_playlist["name"] = playlist->GetName().CStr();
		json_playlist["fileName"] = playlist->GetFileName().CStr();

		Json::Value json_options;
		json_options["webrtcAutoAbr"] = playlist->IsWebRtcAutoAbr();
		json_options["hlsChunklistPathDepth"] = playlist->GetHlsChunklistPathDepth();
		json_options["enableTsPackaging"] = playlist->IsTsPackagingEnabled();

		json_playlist["options"] = json_options;

		for (const auto &rendition : playlist->GetRenditionList())
		{
			Json::Value json_rendition;

			json_rendition["name"] = rendition->GetName().CStr();
			json_rendition["videoTrackName"] = rendition->GetVideoVariantName().CStr();
			json_rendition["videoIndexHint"] = rendition->GetVideoIndexHint();
			json_rendition["audioTrackName"] = rendition->GetAudioVariantName().CStr();
			json_rendition["audioIndexHint"] = rendition->GetAudioIndexHint();

			json_playlist["renditions"].append(json_rendition);
		}

		json_playlists.append(json_playlist);
	}

	for(auto &track_item : _tracks)
	{
		auto &track = track_item.second;

		Json::Value json_track;
		Json::Value json_video_track;
		Json::Value json_audio_track;

		json_track["id"] = track->GetId();
		json_track["name"] = track->GetVariantName().CStr();
		json_track["publicName"] = track->GetPublicName().CStr();
		json_track["language"] = track->GetLanguage().CStr();
		json_track["characteristics"] = track->GetCharacteristics().CStr();
		json_track["codecId"] = static_cast<int8_t>(track->GetCodecId());
		json_track["mediaType"] = static_cast<int8_t>(track->GetMediaType());
		json_track["timebaseNum"] = track->GetTimeBase().GetNum();
		json_track["timebaseDen"] = track->GetTimeBase().GetDen();
		json_track["bitrate"] = track->GetBitrate();
		json_track["startFrameTime"] = track->GetStartFrameTime();
		json_track["lastFrameTime"] = track->GetLastFrameTime();

		json_video_track["framerate"] = track->GetFrameRate();
		json_video_track["width"] = track->GetWidth();
		json_video_track["height"] = track->GetHeight();
		json_video_track["maxWidth"] = track->GetMaxWidth();
		json_video_track["maxHeight"] = track->GetMaxHeight();

		json_audio_track["samplerate"] = track->GetSampleRate();
		json_audio_track["sampleFormat"] = static_cast<int8_t>(track->GetSample().GetFormat());
		json_audio_track["layout"] = static_cast<uint32_t>(track->GetChannel().GetLayout());

		json_track["videoTrack"] = json_video_track;
		json_track["audioTrack"] = json_audio_track;

		auto decoder_config = track->GetDecoderConfigurationRecord();
		if (decoder_config != nullptr)
		{
			json_track["decoderConfig"] = ov::Base64::Encode(decoder_config->GetData()).CStr();
		}
		
		json_tracks.append(json_track);
	}

	json_stream["playlists"] = json_playlists;
	json_stream["tracks"] = json_tracks;
	json_root["stream"] = json_stream;
	
	_description = json_root;

	return true;
}

void OvtStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if(GetState() != Stream::State::STARTED)
	{
		return;
	}

	//logti("Recv Video Frame : pts(%lld) data_len(%lld)", media_packet->GetPts(), media_packet->GetDataLength());

	// Callback OnOvtPacketized()
	std::shared_lock<std::shared_mutex> mlock(_packetizer_lock);
	if(_packetizer != nullptr)
	{
		_packetizer->PacketizeMediaPacket(media_packet->GetPts(), media_packet);
	}
}

void OvtStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if(GetState() != Stream::State::STARTED)
	{
		return;
	}

	// Callback OnOvtPacketized()
	std::shared_lock<std::shared_mutex> mlock(_packetizer_lock);
	if(_packetizer != nullptr)
	{
		_packetizer->PacketizeMediaPacket(media_packet->GetPts(), media_packet);
	}
}

bool OvtStream::OnOvtPacketized(std::shared_ptr<OvtPacket> &packet)
{
	// Broadcasting
	auto stream_packet = std::make_any<std::shared_ptr<OvtPacket>>(packet);
	BroadcastPacket(stream_packet);
	
	
	MonitorInstance->IncreaseBytesOut(*pub::Stream::GetSharedPtrAs<info::Stream>(), PublisherType::Ovt, packet->GetDataLength() * GetSessionCount());

	return true;
}

bool OvtStream::GetDescription(Json::Value &description)
{
	if(GetState() != Stream::State::STARTED)
	{
		return false;
	}
	
	GenerateDescription();
	description = _description;

	return true;
}

bool OvtStream::RemoveSessionByConnectorId(int connector_id)
{
	auto sessions = GetAllSessions();

	logtt("RemoveSessionByConnectorId : all(%zu) connector(%d)", sessions.size(), connector_id);

	for(const auto &item : sessions)
	{
		auto session = std::static_pointer_cast<OvtSession>(item.second);
		logtt("session : %d %d", session->GetId(), session->GetConnector()->GetNativeHandle());

		if(session->GetConnector()->GetNativeHandle() == connector_id)
		{
			RemoveSession(session->GetId());
			return true;
		}
	}

	return false;
}