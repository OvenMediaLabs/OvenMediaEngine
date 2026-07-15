

#include "ovt_private.h"
#include "ovt_stream.h"
#include "ovt_session.h"
#include "base/info/media_track_group.h"
#include "base/info/track_set.h"
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
	// Also check the linked input stream (e.g., RTSP provider pulled via OriginMapStore)
	// to stay consistent with the Stop() logic.
	bool is_from_origin_map_store = IsFromOriginMapStore() || (GetLinkedInputStream() != nullptr && GetLinkedInputStream()->IsFromOriginMapStore());
	if (is_from_origin_map_store == false)
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

bool OvtStream::GenerateDescription(Json::Value &out_description)
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
		json_video_track["maxFramerate"] = track->GetMaxFrameRate();
		auto resolution = track->GetResolution();
		json_video_track["width"] = resolution.width;
		json_video_track["height"] = resolution.height;
		auto max_resolution = track->GetMaxResolution();
		json_video_track["maxWidth"] = max_resolution.width;
		json_video_track["maxHeight"] = max_resolution.height;

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

	out_description = std::move(json_root);

	return true;
}

void OvtStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if(GetState() != Stream::State::STARTED)
	{
		return;
	}

	//logti("Recv Video Frame : pts(%" PRId64 ") data_len(%" PRId64 ")", media_packet->GetPts(), media_packet->GetDataLength());

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

	return GenerateDescription(description);
}

bool OvtStream::GetDescription(const ov::String &track_set_name, Json::Value &description)
{
	if (GetState() != Stream::State::STARTED)
	{
		return false;
	}

	std::set<uint32_t> allowed_track_ids;
	size_t video_count = 0;
	size_t audio_count = 0;
	if (ResolveTrackSetTrackIds(track_set_name, allowed_track_ids, video_count, audio_count) == false)
	{
		return false;
	}

	if (GenerateDescription(description) == false)
	{
		return false;
	}

	FilterDescriptionByTrackIds(description, allowed_track_ids);

	return true;
}

bool OvtStream::ResolveTrackSetTrackIds(const ov::String &track_set_name, std::set<uint32_t> &out_track_ids,
										size_t &out_video_count, size_t &out_audio_count)
{
	out_track_ids.clear();
	out_video_count = 0;
	out_audio_count = 0;

	auto track_set	= GetTrackSet(track_set_name);
	if (track_set == nullptr)
	{
		return false;
	}

	auto collect = [&](const std::vector<std::shared_ptr<info::TrackSetEntry>> &entries, size_t &counter) {
		for (const auto &entry : entries)
		{
			auto group = GetMediaTrackGroup(entry->GetVariantName());
			if (group == nullptr)
			{
				logtw("TrackSet [%s] references missing variant [%s] on stream %s/%s",
					  track_set_name.CStr(), entry->GetVariantName().CStr(),
					  GetApplicationName(), GetName().CStr());
				continue;
			}

			int index_hint = entry->GetIndexHint();
			if (index_hint >= 0)
			{
				auto track = group->GetTrack(static_cast<uint32_t>(index_hint));
				if (track == nullptr)
				{
					logtw("TrackSet [%s] references variant [%s] index %d but group has %zu tracks",
						  track_set_name.CStr(), entry->GetVariantName().CStr(),
						  index_hint, group->GetTrackCount());
					continue;
				}
				if (out_track_ids.insert(track->GetId()).second)
				{
					counter++;
				}
			}
			else
			{
				for (const auto &track : group->GetTracks())
				{
					if (track == nullptr)
					{
						continue;
					}
					if (out_track_ids.insert(track->GetId()).second)
					{
						counter++;
					}
				}
			}
		}
	};

	collect(track_set->GetVideoEntries(), out_video_count);
	collect(track_set->GetAudioEntries(), out_audio_count);

	return true;
}

void OvtStream::FilterDescriptionByTrackIds(Json::Value &description, const std::set<uint32_t> &allowed_track_ids)
{
	Json::Value &stream = description["stream"];
	if (stream.isNull())
	{
		return;
	}

	Json::Value filtered_tracks(Json::arrayValue);
	const Json::Value &tracks = stream["tracks"];
	if (tracks.isArray())
	{
		for (const auto &track : tracks)
		{
			if (track["id"].isUInt() == false)
			{
				continue;
			}
			if (allowed_track_ids.find(track["id"].asUInt()) != allowed_track_ids.end())
			{
				filtered_tracks.append(track);
			}
		}
	}
	stream["tracks"]	= filtered_tracks;

	// Playlists reference full-stream variants; their renditions are not meaningful
	// for a TrackSet-filtered view, so drop them.
	stream["playlists"] = Json::Value(Json::arrayValue);
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