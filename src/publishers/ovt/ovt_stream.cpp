

#include "ovt_stream.h"

#include <base/ovlibrary/clock.h>
#include <base/ovlibrary/json.h>
#include <modules/ovt_packetizer/ovt_packet.h>
#include <modules/ovt_packetizer/ovt_signaling.h>
#include <modules/ovt_packetizer/ovt_wire.h>
#include <orchestrator/orchestrator.h>

#include "base/info/media_track_group.h"
#include "base/info/track_set.h"
#include "base/publisher/application.h"
#include "base/publisher/stream.h"
#include "ovt_private.h"
#include "ovt_session.h"

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
	logtt("OvtStream(%s/%s) has been started", GetApplicationName(), GetName().CStr());
}

OvtStream::~OvtStream()
{
	logtt("OvtStream(%s/%s) has been terminated finally", GetApplicationName(), GetName().CStr());
}

bool OvtStream::Start()
{
	if (GetState() != Stream::State::CREATED)
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

	if (!CreateStreamWorker(_worker_count))
	{
		return false;
	}

	logtt("OvtStream(%d) has been started", GetId());
	_packetizer = std::make_shared<OvtPacketizer>(OvtPacketizerInterface::GetSharedPtr());

	return Stream::Start();
}

bool OvtStream::Stop()
{
	if (GetState() != Stream::State::STARTED)
	{
		return false;
	}

	logtt("OvtStream(%u) has been stopped", GetId());

	if (GetLinkedInputStream() != nullptr && GetLinkedInputStream()->IsFromOriginMapStore() == false)
	{
		// Unegister stream if OriginMapStore is enabled.
		// A failure here is logged and nothing more: the rest of this function is the teardown every
		// edge depends on (the stop notification, sessions, the socket, the packetizer),
		// and leaving it undone strands them on a stream the origin has already dropped.
		auto result = ocst::Orchestrator::GetInstance()->UnregisterStreamFromOriginMapStore(GetApplicationInfo().GetVHostAppName(), GetName());
		if (result == CommonErrorCode::ERROR)
		{
			logtw("Failed to unregister stream from origin map store : %s/%s", GetApplicationName(), GetName().CStr());
		}
	}

	NotifyStop((GetStopCause() == StopCause::ApplicationStopped) ? ovt::StopReason::OriginShutdown : ovt::StopReason::StreamDeleted);

	std::unique_lock<std::shared_mutex> mlock(_packetizer_lock);
	if (_packetizer != nullptr)
	{
		_packetizer->Release();
		_packetizer.reset();
	}
	mlock.unlock();

	return Stream::Stop();
}

// Every edge release treats `application: "stop"` as a normal finish,
// so the origin reuses that name and carries the reason in `message`.
// Sent to each session directly rather than broadcast,
// so a session still behind the `_sent_ready` gate receives it as well.
void OvtStream::NotifyStop(ovt::StopReason reason)
{
	Json::Value root;
	root["application"] = ovt::APPLICATION_STOP;
	root["message"]		= ovt::ToString(reason);
	auto payload		= ov::Json::Stringify(root);

	auto sessions		= GetAllSessions();
	for (const auto &[session_id, session] : sessions)
	{
		std::static_pointer_cast<OvtSession>(session)->SendMessageDirect(OvtPayloadType::MessageResponse, payload);
	}

	if (sessions.empty() == false)
	{
		logti("Notified %zu edge session(s) that %s/%s stopped (%s)", sessions.size(), GetApplicationName(), GetName().CStr(), ovt::ToString(reason));
	}
}

void OvtStream::GenerateDescription(Json::Value &out_description)
{
	// Since the OVT publisher is also an output stream, it transmits the UUID of the input stream.
	auto origin_stream_uuid = (GetLinkedInputStream() != nullptr) ? GetLinkedInputStream()->GetUUID() : GetUUID();

	out_description			= BuildDescription(GetApplicationName(), GetName(), origin_stream_uuid, GetPlaylists(), GetTracks());
}

void OvtStream::GenerateTrackDescription(const std::shared_ptr<const MediaTrack> &track, Json::Value &out_json_track)
{
	Json::Value json_video_track;
	Json::Value json_audio_track;

	out_json_track["id"]			  = track->GetId();
	out_json_track["name"]			  = track->GetVariantName().CStr();
	out_json_track["publicName"]	  = track->GetPublicName().CStr();
	out_json_track["language"]		  = track->GetLanguage().CStr();
	out_json_track["characteristics"] = track->GetCharacteristics().CStr();
	// Send-side boundary of the OVT wire table (`ovt_wire.h`);
	// the receive side is the OVT provider's `ParseTrackFromJson()`.
	// Describe carries the 8-bit wire value as a JSON integer, and OVT1 wrote it through `int8_t`,
	// so the signed spelling is kept.
	out_json_track["codecId"]		  = ovt::ToOvtWire(track->GetCodecId());
	out_json_track["mediaType"]		  = static_cast<int8_t>(ovt::ToOvtWire(track->GetMediaType()));
	// OVT2 names.
	// An OVT2 edge decides on these; the integers above stay for OVT1 edges and as a cross-check.
	out_json_track["codec"]			  = cmn::GetCodecIdString(track->GetCodecId());
	out_json_track["mediaTypeName"]	  = cmn::GetMediaTypeString(track->GetMediaType());
	auto codecs						  = track->GetCodecsParameter();
	if (codecs.IsEmpty() == false)
	{
		out_json_track["codecs"] = codecs.CStr();
	}
	out_json_track["timebaseNum"]		 = track->GetTimeBase().GetNum();
	out_json_track["timebaseDen"]		 = track->GetTimeBase().GetDen();
	out_json_track["bitrate"]			 = track->GetBitrate();
	// Kept for wire compatibility; the receiver measures its own frame times
	out_json_track["startFrameTime"]	 = 0;
	out_json_track["lastFrameTime"]		 = 0;

	json_video_track["framerate"]		 = track->GetFrameRate();
	json_video_track["maxFramerate"]	 = track->GetMaxFrameRate();
	auto resolution						 = track->GetResolution();
	json_video_track["width"]			 = resolution.width;
	json_video_track["height"]			 = resolution.height;
	auto max_resolution					 = track->GetMaxResolution();
	json_video_track["maxWidth"]		 = max_resolution.width;
	json_video_track["maxHeight"]		 = max_resolution.height;

	json_audio_track["samplerate"]		 = track->GetSampleRate();
	json_audio_track["sampleFormat"]	 = static_cast<int8_t>(ovt::ToOvtWire(track->GetSample().GetFormat()));
	json_audio_track["sampleFormatName"] = track->GetSample().GetName();
	// A value this build has no layout for went out as it came in
	auto unmapped_layout				 = track->GetChannel().GetUnmappedLayout();
	json_audio_track["layout"]			 = unmapped_layout.value_or(static_cast<uint32_t>(track->GetChannel().GetLayout()));

	out_json_track["videoTrack"]		 = json_video_track;
	out_json_track["audioTrack"]		 = json_audio_track;

	auto decoder_config					 = track->GetDecoderConfigurationRecord();
	if (decoder_config != nullptr)
	{
		out_json_track["decoderConfig"] = ov::Base64::Encode(decoder_config->GetData()).CStr();
	}
}

Json::Value OvtStream::BuildDescription(const ov::String &app_name, const ov::String &stream_name, const ov::String &origin_stream_uuid,
										const std::map<ov::String, std::shared_ptr<const info::Playlist>> &playlists,
										const std::map<int32_t, std::shared_ptr<const MediaTrack>> &tracks)
{
	Json::Value json_root;
	Json::Value json_stream;
	Json::Value json_tracks;
	// Always an array, including when there is none.
	// `v0.21.0.0` left it null in that case and every release either ignores the key or reads it with
	// `size()`, where null and an empty array behave the same, so one shape can be used throughout.
	Json::Value json_playlists(Json::arrayValue);

	json_root["version"]			= OVT_SIGNALING_VERSION;

	json_stream["appName"]			= app_name.CStr();
	json_stream["streamName"]		= stream_name.CStr();
	json_stream["originStreamUUID"] = origin_stream_uuid.CStr();

	for (const auto &[file_name, playlist] : playlists)
	{
		Json::Value json_playlist;

		json_playlist["name"]			 = playlist->GetName().CStr();
		json_playlist["fileName"]		 = playlist->GetFileName().CStr();
		json_playlist["enableSubtitles"] = playlist->IsSubtitlesEnabled();

		Json::Value json_options;
		json_options["webrtcAutoAbr"]		  = playlist->IsWebRtcAutoAbr();
		json_options["hlsChunklistPathDepth"] = playlist->GetHlsChunklistPathDepth();
		json_options["enableTsPackaging"]	  = playlist->IsTsPackagingEnabled();

		json_playlist["options"]			  = json_options;

		for (const auto &rendition : playlist->GetRenditionList())
		{
			Json::Value json_rendition;

			json_rendition["name"]			 = rendition->GetName().CStr();
			json_rendition["videoTrackName"] = rendition->GetVideoVariantName().CStr();
			json_rendition["videoIndexHint"] = rendition->GetVideoIndexHint();
			json_rendition["audioTrackName"] = rendition->GetAudioVariantName().CStr();
			json_rendition["audioIndexHint"] = rendition->GetAudioIndexHint();
			// OVT2 track references. An OVT1 edge reads the index hints instead and ignores these.
			if (rendition->GetVideoTrackId().has_value())
			{
				json_rendition["videoTrackId"] = *rendition->GetVideoTrackId();
			}
			if (rendition->GetAudioTrackId().has_value())
			{
				json_rendition["audioTrackId"] = *rendition->GetAudioTrackId();
			}

			json_playlist["renditions"].append(json_rendition);
		}

		json_playlists.append(json_playlist);
	}

	for (const auto &item : tracks)
	{
		Json::Value json_track;
		GenerateTrackDescription(item.second, json_track);
		json_tracks.append(json_track);
	}

	json_stream["playlists"] = json_playlists;
	json_stream["tracks"]	 = json_tracks;
	json_root["stream"]		 = json_stream;

	return json_root;
}

// An OVT1 edge adds the tracks in the order received and `MediaTrackGroup::AddTrack()` numbers each
// variant from 0 in that order, so the same walk here yields exactly the index the edge will use.
void OvtStream::RenumberIndexHints(Json::Value &stream)
{
	if (stream.isObject() == false)
	{
		return;
	}

	std::map<ov::String, int> next_index;
	std::map<uint32_t, int> new_index;
	for (const auto &track : stream["tracks"])
	{
		new_index[track["id"].asUInt()] = next_index[track["name"].asCString()]++;
	}

	for (auto &playlist : stream["playlists"])
	{
		for (auto &rendition : playlist["renditions"])
		{
			if (rendition["videoTrackId"].isUInt() && (new_index.count(rendition["videoTrackId"].asUInt()) > 0))
			{
				rendition["videoIndexHint"] = new_index[rendition["videoTrackId"].asUInt()];
			}
			if (rendition["audioTrackId"].isUInt() && (new_index.count(rendition["audioTrackId"].asUInt()) > 0))
			{
				rendition["audioIndexHint"] = new_index[rendition["audioTrackId"].asUInt()];
			}
		}
	}
}

// Same shape as the NOTIFY a track change broadcasts, with the tracks the caller passes instead of one.
// The caller decides which tracks belong in it: a session with a TrackSet passes only the ones it
// subscribed to, so the snapshot says what that session's describe would say today.
// The per-change NOTIFY is broadcast to every session unfiltered, and the edge skips what it does not have.
ov::String OvtStream::BuildTrackSnapshot(const std::map<int32_t, std::shared_ptr<const MediaTrack>> &tracks)
{
	Json::Value json_tracks(Json::arrayValue);
	for (const auto &[track_id, track] : tracks)
	{
		Json::Value json_track;
		GenerateTrackDescription(track, json_track);
		json_tracks.append(json_track);
	}

	if (json_tracks.empty())
	{
		return "";
	}

	Json::Value json_stream;
	json_stream["tracks"] = json_tracks;

	Json::Value contents;
	contents["version"] = OVT_SIGNALING_VERSION;
	contents["stream"]	= json_stream;

	Json::Value root;
	root["id"]			= 0;
	root["application"] = ovt::APPLICATION_NOTIFY;
	root["code"]		= 200;
	root["message"]		= "track_changed";
	root["contents"]	= contents;

	return ov::Json::Stringify(root);
}

void OvtStream::OnTrackChanged(int32_t track_id, const std::shared_ptr<const MediaTrack> &old_track, const std::shared_ptr<const MediaTrack> &new_track)
{
	if (GetState() != Stream::State::STARTED)
	{
		return;
	}

	// A label-only change does not affect the media configuration, so there is
	// nothing for the edge to re-apply. Defer to the base metadata-update handling
	// and skip the edge relay.
	if (old_track->HasSameContent(*new_track))
	{
		Stream::OnTrackChanged(track_id, old_track, new_track);
		return;
	}

	// A session whose describe reflected an earlier value learns it is stale from this.
	// Raised before the broadcast, so a session that reads it in between sees the change as pending
	// and catches up with a snapshot; the other order would let it read the old value and miss both.
	_track_epoch.fetch_add(1, std::memory_order_release);

	// Relay the changed track's already-parsed configuration to connected edges.
	// OnTrackChanged runs right before the first media packet of the new version
	// is broadcast, so the message reaches the edge ahead of that packet on the
	// same connection. Only the changed track is sent; the edge replaces its own
	// copy and skips tracks it did not subscribe to.
	// The same message a catch-up snapshot sends, carrying this one track instead of all of them
	auto payload = BuildTrackSnapshot({{track_id, new_track}}).ToData(false);

	// An OVT1 edge has no wire value for this codec and no way to be told;
	// it receives the notification as before and its stream stalls on that track.
	// Logged on the origin so the operator can see it.
	if (ovt::HasLegacyWireValue(new_track->GetCodecId()) == false)
	{
		size_t legacy_sessions = 0;
		for (const auto &[session_id, session] : GetAllSessions())
		{
			legacy_sessions += std::static_pointer_cast<OvtSession>(session)->IsOvt2() ? 0 : 1;
		}

		if (legacy_sessions > 0)
		{
			logtw("%s/%s(%u) Track(%d) changed to %s, which %zu OVT1 edge session(s) cannot carry",
				  GetApplicationName(), GetName().CStr(), GetId(), track_id, cmn::GetCodecIdString(new_track->GetCodecId()), legacy_sessions);
		}
	}

	std::shared_lock<std::shared_mutex> mlock(_packetizer_lock);
	if (_packetizer == nullptr)
	{
		return;
	}

	// Routed through OnOvtPacketized() -> BroadcastPacket() to every session
	if (_packetizer->PacketizeMessage(OvtPayloadType::MessageResponse, ov::Clock::NowMSec(), payload) == false)
	{
		logtw("%s/%s(%u) Failed to relay Track(%d) configuration change to edges (version %u -> %u)",
			  GetApplicationName(), GetName().CStr(), GetId(), track_id, old_track->GetVersion(), new_track->GetVersion());
		return;
	}

	logti("%s/%s(%u) Relayed Track(%d) configuration change to edges (version %u -> %u)",
		  GetApplicationName(), GetName().CStr(), GetId(), track_id, old_track->GetVersion(), new_track->GetVersion());
}

void OvtStream::NoteRequiredValues(const std::shared_ptr<const MediaPacket> &media_packet)
{
	if (ovt::HasLegacyWireValue(media_packet->GetBitstreamFormat()) == false)
	{
		if (_required_set.Add(ovt::BitstreamToken(media_packet->GetBitstreamFormat())))
		{
			logti("Stream %s/%s now requires %s", GetApplicationName(), GetName().CStr(), ovt::BitstreamToken(media_packet->GetBitstreamFormat()).CStr());
		}
	}

	if (ovt::HasLegacyWireValue(media_packet->GetPacketType()) == false)
	{
		if (_required_set.Add(ovt::PacketTypeToken(media_packet->GetPacketType())))
		{
			logti("Stream %s/%s now requires %s", GetApplicationName(), GetName().CStr(), ovt::PacketTypeToken(media_packet->GetPacketType()).CStr());
		}
	}
}

std::vector<ov::String> OvtStream::CollectRequiredTokens(const Json::Value &contents, const std::set<cmn::MediaType> &required_media_types)
{
	std::set<ov::String> tokens;
	for (const auto &track : contents["stream"]["tracks"])
	{
		auto media_type = cmn::GetMediaTypeByName(track["mediaTypeName"].asCString());
		if ((media_type.has_value() == false) || (required_media_types.count(*media_type) == 0))
		{
			continue;
		}

		tokens.insert(ovt::CodecToken(track["codec"].asCString()));
	}

	return std::vector<ov::String>(tokens.begin(), tokens.end());
}

void OvtStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (GetState() != Stream::State::STARTED)
	{
		return;
	}

	NoteRequiredValues(media_packet);

	//logti("Recv Video Frame : pts(%" PRId64 ") data_len(%" PRId64 ")", media_packet->GetPts(), media_packet->GetDataLength());

	// Callback OnOvtPacketized()
	std::shared_lock<std::shared_mutex> mlock(_packetizer_lock);
	if (_packetizer != nullptr)
	{
		_packetizer->PacketizeMediaPacket(media_packet->GetPts(), media_packet);
	}
}

void OvtStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (GetState() != Stream::State::STARTED)
	{
		return;
	}

	NoteRequiredValues(media_packet);

	// Callback OnOvtPacketized()
	std::shared_lock<std::shared_mutex> mlock(_packetizer_lock);
	if (_packetizer != nullptr)
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

bool OvtStream::IsCarriableByLegacyEdge(const Json::Value &track)
{
	// `Data` carries no codec, so `codecId` is `None` for it as well as for a codec this build
	// has no value for. The name is what tells the two apart.
	if (ov::String(track["mediaTypeName"].asCString()) == cmn::GetMediaTypeString(cmn::MediaType::Data))
	{
		return true;
	}

	auto codec_id = cmn::GetCodecIdByExactName(track["codec"].asCString());

	return codec_id.has_value() && ovt::HasLegacyWireValue(*codec_id);
}

size_t OvtStream::RebuildRenditions(Json::Value &stream, const std::set<uint32_t> &allowed)
{
	if (stream.isObject() == false)
	{
		return 0;
	}

	// Surviving tracks, and the variant names and ids they leave behind
	Json::Value kept_tracks(Json::arrayValue);
	std::set<ov::String> variants;
	std::map<ov::String, std::set<uint32_t>> variant_track_ids;
	for (const auto &track : stream["tracks"])
	{
		if ((track["id"].isUInt() == false) || (allowed.count(track["id"].asUInt()) == 0))
		{
			continue;
		}

		kept_tracks.append(track);
		ov::String variant = track["name"].asCString();
		variants.insert(variant);
		variant_track_ids[variant].insert(track["id"].asUInt());
	}
	stream["tracks"] = kept_tracks;

	// Each rendition side: a variant that vanished empties the side,
	// an id whose track was removed empties it too (that exact track is gone),
	// a surviving id stays, and a side without an id keeps its group meaning.
	// A rendition with both sides empty goes,
	// and so does a later rendition whose resolved track set repeats an earlier one
	// (HLS would package it twice).
	// The index hints keep their values here: HLS names its files by them.
	size_t pruned	 = 0;
	Json::Value kept_playlists(Json::arrayValue);
	for (auto &playlist : stream["playlists"])
	{
		Json::Value kept_renditions(Json::arrayValue);
		std::vector<std::set<uint32_t>> seen_track_sets;

		for (auto &rendition : playlist["renditions"])
		{
			std::set<uint32_t> resolved;
			// A side the filter empties keeps the variant it used to name, under `removed_key`.
			// Without it the result is indistinguishable from a side the operator left empty on purpose,
			// so an edge could not tell a rendition the filter broke from one that is meant to be one-sided.
			auto rebuild_side = [&](const char *name_key, const char *hint_key, const char *id_key, const char *removed_key) {
				ov::String variant = rendition[name_key].asCString();
				if (variant.IsEmpty())
				{
					return;
				}

				if (variants.count(variant) == 0)
				{
					rendition[name_key] = "";
					rendition[hint_key] = -1;
					rendition.removeMember(id_key);
					rendition[removed_key] = variant.CStr();
					return;
				}

				// `isMember()` first: `operator[]` on a non-const value would insert the key as null,
				// and a rendition that never carried a track id must not grow one here.
				if (rendition.isMember(id_key) && rendition[id_key].isUInt())
				{
					if (allowed.count(rendition[id_key].asUInt()) == 0)
					{
						rendition[name_key] = "";
						rendition[hint_key] = -1;
						rendition.removeMember(id_key);
						rendition[removed_key] = variant.CStr();
						return;
					}

					resolved.insert(rendition[id_key].asUInt());
					return;
				}

				const auto &ids = variant_track_ids[variant];
				resolved.insert(ids.begin(), ids.end());
			};

			rebuild_side("videoTrackName", "videoIndexHint", "videoTrackId", "removedVideoTrackName");
			rebuild_side("audioTrackName", "audioIndexHint", "audioTrackId", "removedAudioTrackName");

			if (ov::String(rendition["videoTrackName"].asCString()).IsEmpty() && ov::String(rendition["audioTrackName"].asCString()).IsEmpty())
			{
				pruned++;
				continue;
			}

			if (std::find(seen_track_sets.begin(), seen_track_sets.end(), resolved) != seen_track_sets.end())
			{
				pruned++;
				continue;
			}

			seen_track_sets.push_back(resolved);
			kept_renditions.append(rendition);
		}

		if (kept_renditions.empty())
		{
			pruned++;
			continue;
		}

		playlist["renditions"] = kept_renditions;
		kept_playlists.append(playlist);
	}
	stream["playlists"] = kept_playlists;

	return pruned;
}

bool OvtStream::GetDescription(const OvtTrackFilter &filter, Json::Value &description, bool *filtered)
{
	if (filtered != nullptr)
	{
		*filtered = false;
	}

	if (GetState() != Stream::State::STARTED)
	{
		return false;
	}

	GenerateDescription(description);

	if (filter.IsActive() == false)
	{
		return true;
	}

	Json::Value &stream = description["stream"];
	std::set<uint32_t> allowed;
	for (const auto &track : stream["tracks"])
	{
		auto track_id = track["id"].asUInt();
		if (filter.track_set_ids.has_value() && (filter.track_set_ids->count(track_id) == 0))
		{
			continue;
		}

		if (filter.legacy_edge && (IsCarriableByLegacyEdge(track) == false))
		{
			logtw("Track(%u, %s) of %s/%s is not sent to an OVT1 edge: it has no OVT1 wire value",
				  track_id, track["codec"].asCString(), GetApplicationName(), GetName().CStr());
			continue;
		}

		allowed.insert(track_id);
	}

	if (allowed.size() == stream["tracks"].size())
	{
		return true;
	}

	auto pruned = RebuildRenditions(stream, allowed);
	if (pruned > 0)
	{
		logti("Rebuilt the describe of %s/%s for this edge: %zu rendition(s) or playlist(s) removed",
			  GetApplicationName(), GetName().CStr(), pruned);
	}

	if (filtered != nullptr)
	{
		*filtered = true;
	}

	return true;
}

std::optional<std::set<uint32_t>> OvtStream::ResolveAllowedTrackIds(const OvtTrackFilter &filter)
{
	if (filter.IsActive() == false)
	{
		return std::nullopt;
	}

	std::set<uint32_t> allowed;
	auto tracks = GetTracks();
	for (const auto &[track_id, track] : tracks)
	{
		if (filter.Accepts(track_id, track->GetMediaType(), track->GetCodecId()))
		{
			allowed.insert(track_id);
		}
	}

	// An OVT1 edge without a TrackSet keeps the unfiltered session when no track had to be removed
	if ((filter.track_set_ids.has_value() == false) && (filter.requested_track_ids.has_value() == false) &&
		(allowed.size() == tracks.size()))
	{
		return std::nullopt;
	}

	return allowed;
}

Json::Value OvtStream::BuildPlaylistsForTracks(const std::set<uint32_t> &allowed)
{
	Json::Value description;
	GenerateDescription(description);

	Json::Value &stream = description["stream"];
	RebuildRenditions(stream, allowed);

	return stream["playlists"];
}

bool OvtStream::ResolveTrackSetTrackIds(const ov::String &track_set_name, std::set<uint32_t> &out_track_ids)
{
	out_track_ids.clear();

	auto track_set = GetTrackSet(track_set_name);
	if (track_set == nullptr)
	{
		return false;
	}

	auto collect = [&](const std::vector<std::shared_ptr<info::TrackSetEntry>> &entries) {
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
				out_track_ids.insert(track->GetId());
			}
			else
			{
				for (const auto &track : group->GetTracks())
				{
					if (track == nullptr)
					{
						continue;
					}
					out_track_ids.insert(track->GetId());
				}
			}
		}
	};

	collect(track_set->GetVideoEntries());
	collect(track_set->GetAudioEntries());

	return true;
}

bool OvtStream::RemoveSessionByConnectorId(int connector_id)
{
	auto sessions = GetAllSessions();

	logtt("RemoveSessionByConnectorId : all(%zu) connector(%d)", sessions.size(), connector_id);

	for (const auto &item : sessions)
	{
		auto session = std::static_pointer_cast<OvtSession>(item.second);
		logtt("session : %d %d", session->GetId(), session->GetConnector()->GetNativeHandle());

		if (session->GetConnector()->GetNativeHandle() == connector_id)
		{
			RemoveSession(session->GetId());
			return true;
		}
	}

	return false;
}