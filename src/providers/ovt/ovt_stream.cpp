//
// Created by getroot on 19. 12. 9.
//

#include "ovt_stream.h"

#include <modules/bitstream/decoder_configuration_record_parser.h>
#include <modules/ovt_packetizer/ovt_signaling.h>
#include <modules/ovt_packetizer/ovt_wire.h>

#include "base/info/application.h"
#include "ovt_provider.h"

#define OV_LOG_TAG "OvtStream"

namespace pvd
{
	std::shared_ptr<OvtStream> OvtStream::Create(const std::shared_ptr<pvd::PullApplication> &application,
												 const uint32_t stream_id, const ov::String &stream_name,
												 const std::vector<ov::String> &url_list,
												 const std::shared_ptr<pvd::PullStreamProperties> &properties)
	{
		info::Stream stream_info(*std::static_pointer_cast<info::Application>(application), StreamSourceType::Ovt);

		stream_info.SetId(stream_id);
		stream_info.SetName(stream_name);

		auto stream = std::make_shared<OvtStream>(application, stream_info, url_list, properties);
		if (!stream->Start())
		{
			// Explicit deletion
			stream.reset();
			return nullptr;
		}

		return stream;
	}

	OvtStream::OvtStream(const std::shared_ptr<pvd::PullApplication> &application, const info::Stream &stream_info, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties)
		: pvd::PullStream(application, stream_info, url_list, properties)
	{
		_last_request_id = 0;
		SetState(State::IDLE);
		logtt("OvtStream Created : %d", GetId());
	}

	OvtStream::~OvtStream()
	{
		Release();
		Stop();
		logtt("OvtStream Terminated : %d", GetId());
	}

	void OvtStream::Release()
	{
		auto connection = std::atomic_load(&_connection);
		if (connection->socket != nullptr)
		{
			connection->socket->Close();
		}

		_curr_url = nullptr;

		std::lock_guard<std::shared_mutex> mlock(_packetizer_lock);
		if (_packetizer != nullptr)
		{
			_packetizer->Release();
			_packetizer.reset();
		}
	}

	bool OvtStream::StartStream(const std::shared_ptr<const ov::Url> &url)
	{
		// Only start from IDLE, ERROR, STOPPED
		if (!(GetState() == State::IDLE || GetState() == State::ERROR || GetState() == State::STOPPED))
		{
			return true;
		}

		_curr_url = url;

		// A new origin starts with a clean slate: what the previous origin announced or logged is not its.
		// The object is built here and published by `ConnectOrigin()` once its socket is connected,
		// so until then the previous connection stays in place and a thread still reading media
		// through it keeps a socket and a depacketizer that belong together.
		auto next_connection = std::make_shared<Connection>();

		if (_packetizer == nullptr)
		{
			_packetizer = std::make_shared<OvtPacketizer>(OvtPacketizerInterface::GetSharedPtr());
		}

		ov::StopWatch stop_watch;

		stop_watch.Start();
		if (!ConnectOrigin(next_connection))
		{
			SetState(Stream::State::ERROR);
			Release();
			return false;
		}
		_origin_request_time_msec = stop_watch.Elapsed();

		stop_watch.Update();
		if (!RequestDescribe(next_connection))
		{
			SetState(Stream::State::ERROR);
			Release();
			return false;
		}

		if (!RequestPlay(next_connection))
		{
			SetState(Stream::State::ERROR);
			Release();
			return false;
		}
		_origin_response_time_msec = stop_watch.Elapsed();

		// For statistics. Each connection reports its own timings once the stream has a metrics object.
		// The gate opens only after both values are in place: a motor thread that reached
		// `ReportOriginTimes()` in between would otherwise publish the previous connection's timings
		// and close the gate behind them, so the new ones would never go out.
		_origin_times_reported.store(false);
		ReportOriginTimes();

		return true;
	}

	void OvtStream::ReportOriginTimes()
	{
		if (_origin_times_reported.load() == true)
		{
			return;
		}

		auto stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(pvd::Stream::GetSharedPtr()));
		if (stream_metrics == nullptr)
		{
			return;
		}

		if (_origin_times_reported.exchange(true) == true)
		{
			return;
		}

		stream_metrics->SetOriginConnectionTimeMSec(_origin_request_time_msec.load());
		stream_metrics->SetOriginSubscribeTimeMSec(_origin_response_time_msec.load());
	}

	bool OvtStream::RestartStream(const std::shared_ptr<const ov::Url> &url)
	{
		logti("[%s/%s(%u)] stream tries to reconnect to %s", GetApplicationTypeName(), GetName().CStr(), GetId(), url->ToUrlString().CStr());
		if (StartStream(url) == false)
		{
			return false;
		}

		return true;
	}

	bool OvtStream::StopStream()
	{
		if (GetState() == State::STOPPED)
		{
			return true;
		}

		RequestStop();
		Release();

		SetState(State::STOPPED);

		return true;
	}

	std::shared_ptr<pvd::OvtProvider> OvtStream::GetOvtProvider()
	{
		return std::static_pointer_cast<OvtProvider>(GetApplication()->GetParentProvider());
	}

	bool OvtStream::ConnectOrigin(const std::shared_ptr<Connection> &connection)
	{
		if (GetState() == State::PLAYING || GetState() == State::TERMINATED)
		{
			return false;
		}

		if (_curr_url == nullptr)
		{
			logte("Origin url is not set");
			return false;
		}

		auto scheme = _curr_url->Scheme();
		if (scheme.UpperCaseString() != "OVT")
		{
			logte("The scheme is not OVT : %s", scheme.CStr());
			return false;
		}

		auto pool = GetOvtProvider()->GetClientSocketPool();

		if (pool == nullptr)
		{
			// Provider is not initialized
			return false;
		}

		auto socket_address = ov::SocketAddress::CreateAndGetFirst(_curr_url->Host(), _curr_url->Port());

		auto client_socket = pool->AllocSocket(socket_address.GetFamily());

		if (client_socket == nullptr)
		{
			logte("To create client socket is failed.");
			return false;
		}

		client_socket->SetSockOpt<int>(IPPROTO_TCP, TCP_NODELAY, 1);
		client_socket->SetSockOpt<int>(IPPROTO_TCP, TCP_QUICKACK, 1);
		client_socket->MakeBlocking();

		struct timeval tv = {1, 500000};  // 1.5 sec
		client_socket->SetRecvTimeout(tv);

		auto error = client_socket->Connect(socket_address, 1500);
		if (error != nullptr)
		{
			logte("Cannot connect to origin server (%s) : (%s)", error->GetMessage().CStr(), socket_address.ToString().CStr());
			return false;
		}

		connection->socket = client_socket;

		// Complete from here on, so the media path can pick it up
		std::atomic_store(&_connection, connection);

		SetState(State::CONNECTED);

		return true;
	}

	bool OvtStream::RequestDescribe(const std::shared_ptr<Connection> &connection)
	{
		if (GetState() != State::CONNECTED)
		{
			return false;
		}

		Json::Value root;

		_last_request_id++;
		root["id"] = _last_request_id;
		root["application"] = ovt::APPLICATION_DESCRIBE;
		root["target"] = _curr_url->Source().CStr();
		root["ovt"] = ovt::MakeRequestOvtObject();

		auto message = ov::Json::Stringify(root).ToData(false);

		std::shared_lock<std::shared_mutex> lock(_packetizer_lock);
		if (_packetizer->PacketizeMessage(OvtPayloadType::MessageRequest, ov::Clock::NowMSec(), message) == false)
		{
			return false;
		}

		// jsoncpp throws on a type mismatch (`as*()` on another type, operator[] on a non-object),
		// so a malformed response ends this request and not the process
		try
		{
			return ReceiveDescribe(connection, _last_request_id);
		}
		catch (const Json::Exception &e)
		{
			logte("%s/%s(%u) - An invalid describe response : %s", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), e.what());
			return false;
		}
	}

	std::optional<ov::String> OvtStream::GetTrackSkipReason(const Json::Value &json_track, const std::shared_ptr<const MediaTrack> &track)
	{
		// `Data` tracks carry no codec (a `codecId` of `None` is normal) and are always valid,
		// so they are registered as before
		if (json_track["mediaTypeName"].isString() && (ov::String(json_track["mediaTypeName"].asCString()) == cmn::GetMediaTypeString(cmn::MediaType::Data)))
		{
			return std::nullopt;
		}

		if ((json_track["codec"].isString() == false) || (json_track["mediaTypeName"].isString() == false) ||
			((track->GetMediaType() == cmn::MediaType::Audio) && (json_track["audioTrack"]["sampleFormatName"].isString() == false)))
		{
			return ov::String("missing-name");
		}

		if (cmn::GetCodecIdByExactName(json_track["codec"].asCString()).has_value() == false)
		{
			return ov::String("unknown-codec");
		}

		if (cmn::GetMediaTypeByName(json_track["mediaTypeName"].asCString()).has_value() == false)
		{
			return ov::String("unknown-media-type");
		}

		if ((track->GetMediaType() == cmn::MediaType::Audio) &&
			(cmn::GetAudioSampleFormatByName(json_track["audioTrack"]["sampleFormatName"].asCString()).has_value() == false))
		{
			return ov::String("unknown-sample-format");
		}

		if (ovt::TransportBitstreamFormat(track->GetCodecId()).has_value() == false)
		{
			return ov::String("no-transport-format");
		}

		return std::nullopt;
	}

	// An OVT1 origin sends no codec string,
	// so the only cross-check is the pair (`codecId`-derived transport format, MH[30]).
	// Nothing is dropped or rewritten here: a mismatch and an unmapped byte are logged once per track.
	// From an OVT2 origin that never announced any required token,
	// a value outside the table is a protocol violation
	// (the origin must announce a new transport format through PT 40 first);
	// the byte stays as received either way.
	void OvtStream::CheckWireFormat(const std::shared_ptr<Connection> &connection, const std::shared_ptr<const MediaPacket> &media_packet)
	{
		auto track_id = media_packet->GetTrackId();

		auto unmapped = media_packet->GetUnmappedWireValue(MediaPacket::WireField::BitstreamFormat);
		if (unmapped.has_value())
		{
			if (connection->wire_format_logged.insert(track_id).second)
			{
				logtw("[%s/%s(%u)] Track(%u) carries bitstream format byte %u, which is not in the OVT wire table%s",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), track_id, static_cast<unsigned int>(*unmapped),
					  (connection->origin_is_ovt2 && (connection->required_announced == false)) ? " and was never announced as required" : "");
			}
			return;
		}

		auto track = GetTrack(track_id);
		if ((track == nullptr) || (track->GetMediaType() == cmn::MediaType::Data))
		{
			return;
		}

		auto expected = ovt::TransportBitstreamFormat(track->GetCodecId());
		if ((expected.has_value() == false) || (*expected == media_packet->GetBitstreamFormat()))
		{
			return;
		}

		if (connection->wire_format_logged.insert(track_id).second)
		{
			logtw("[%s/%s(%u)] Track(%u) codec %s implies bitstream format %s but the origin sends %s",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), track_id,
				  cmn::GetCodecIdString(track->GetCodecId()), cmn::GetBitstreamFormatString(*expected), cmn::GetBitstreamFormatString(media_packet->GetBitstreamFormat()));
		}
	}

	bool OvtStream::CheckRequiredTokens(const Json::Value &json_required, const char *source)
	{
		// An absent array is an empty set; a malformed one is a broken origin
		if (json_required.isNull())
		{
			return true;
		}

		auto tokens = ovt::ParseRequiredTokens(json_required);
		if (tokens.has_value() == false)
		{
			logte("[%s/%s(%u)] The %s required list from the origin is not an array of strings",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), source);
			return false;
		}

		// The tokens are the origin's own strings and the ones this build does not know go to a log line,
		// so they are cut and stripped of control characters on the way there.
		std::vector<ov::String> unknown;
		for (const auto &token : *tokens)
		{
			if (ovt::IsSupportedRequiredToken(token) == false)
			{
				unknown.push_back(ovt::SanitizeForLog(token));
			}
		}

		if (unknown.empty() == false)
		{
			logte("[%s/%s(%u)] Refusing the stream: the origin requires %s (%s), which this build does not support",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), ov::String::Join(unknown, ", ").CStr(), source);
			return false;
		}

		return true;
	}

	PullStream::ProcessMediaResult OvtStream::ProcessMessage(const std::shared_ptr<Connection> &connection, const OvtDepacketizer::Message &message)
	{
		if (message.data == nullptr)
		{
			return PullStream::ProcessMediaResult::PROCESS_MEDIA_FAILURE;
		}

		// PT 40 announces required tokens learned after describe;
		// an unknown one ends the stream at once, before the media that carries the value is interpreted.
		// `Parse()` throws on a payload the parser refuses (nesting past jsoncpp's stack limit, for one),
		// so this shares the same guard as the response path below.
		if (message.payload_type == OvtPayloadType::Required)
		{
			ov::String payload(message.data->GetDataAs<char>(), message.data->GetLength());

			try
			{
				auto object = ov::Json::Parse(payload);
				if (object.IsNull() || (object.GetJsonValue().isObject() == false))
				{
					logte("[%s/%s(%u)] Invalid required message from the origin", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
					return PullStream::ProcessMediaResult::PROCESS_MEDIA_FAILURE;
				}

				connection->required_announced = true;
				return CheckRequiredTokens(object.GetJsonValue()["required"], "PT 40") ? PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS
																					   : PullStream::ProcessMediaResult::PROCESS_MEDIA_FAILURE;
			}
			catch (const Json::Exception &e)
			{
				logte("[%s/%s(%u)] Invalid required message from the origin : %s",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), e.what());
				return PullStream::ProcessMediaResult::PROCESS_MEDIA_FAILURE;
			}
		}

		// Origin-initiated signaling (notify, stop) arrives as `MessageResponse`; no other type is understood yet
		if (message.payload_type != OvtPayloadType::MessageResponse)
		{
			logtd("[%s/%s(%u)] Ignored a message with payload type %u",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), ov::ToUnderlyingType(message.payload_type));
			return PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS;
		}

		try
		{
			// Parsing Payload
			ov::String payload(message.data->GetDataAs<char>(), message.data->GetLength());
			auto object = ov::Json::Parse(payload);

			if (object.IsNull())
			{
				logte("An invalid response : Json format");
				return PullStream::ProcessMediaResult::PROCESS_MEDIA_FAILURE;
			}

			// The message kind. A non-string is read as empty and falls through to the ignore path below.
			const Json::Value &json_application = object.GetJsonValue()["application"];
			ov::String application				= json_application.isString() ? json_application.asString().c_str() : "";

			if (ovt::IsApplication(application, ovt::APPLICATION_STOP))
			{
				// Both directions use this message name, so the log spells out which one it is:
				// an answer to this edge's own request reflects `id` or `code`, an origin-initiated
				// notification carries neither. The reason is logged either way.
				auto &json_reason = object.GetJsonValue()["message"];
				ov::String reason = json_reason.isString() ? json_reason.asString().c_str() : "";
				auto known_reason = ovt::ParseStopReason(reason);
				bool is_answer	  = (object.GetJsonValue()["id"].isNull() == false) ||
									(object.GetJsonValue()["code"].isNull() == false);

				logti("[%s/%s(%u)] %s stopped the stream (reason: %s)",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
					  is_answer ? "This edge" : "Origin",
					  known_reason.has_value() ? ovt::ToString(*known_reason) : (reason.IsEmpty() ? "-" : reason.CStr()));

				return PullStream::ProcessMediaResult::PROCESS_MEDIA_FINISH;
			}
			else if (ovt::IsApplication(application, ovt::APPLICATION_NOTIFY))
			{
				// Only the track configuration relay is understood. Ignore any other
				// NOTIFY kind (or a malformed payload) so it cannot be misapplied.
				auto &json_notify = object.GetJsonValue()["message"];
				if (json_notify.isString() && ov::String(json_notify.asString().c_str()) == "track_changed")
				{
					if (ApplyTrackNotification(connection, object.GetJsonValue()["contents"]) == false)
					{
						return PullStream::ProcessMediaResult::PROCESS_MEDIA_FAILURE;
					}
				}
				else
				{
					logtd("[%s/%s(%u)] Ignored unknown NOTIFY message",
						  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
				}

				return PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS;
			}

			// A kind this build does not know is ignored, the same as an unknown payload type and an
			// unknown NOTIFY kind. A later release may add an optional kind, and an edge that tears the
			// stream down over it can never be made tolerant afterwards; a required one is refused at
			// describe through `ovt.required` instead.
			logtd("[%s/%s(%u)] Ignored a message of an unknown kind (application: %s)",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
				  ovt::SanitizeForLog(application).CStr());

			return PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS;
		}
		catch (const Json::Exception &e)
		{
			logte("%s/%s(%u) - An invalid message from origin : %s", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), e.what());
			return PullStream::ProcessMediaResult::PROCESS_MEDIA_FAILURE;
		}
	}

	bool OvtStream::RegisterDescribedTracks(const std::shared_ptr<Connection> &connection, const std::optional<std::set<uint32_t>> &allowed_track_ids)
	{
		// This object's playlists are replaced by the new origin's set.
		// Consumers that copied the stream when it was created (mediarouter, publishers) keep their copies;
		// there is no update path to them.
		ClearPlaylists();
		for (const auto &playlist : _described_playlists)
		{
			AddPlaylist(playlist);
		}
		_described_playlists.clear();

		// Tracks are added in the order the origin listed them.
		// `MediaTrackGroup::AddTrack()` numbers each variant by insertion order,
		// and an origin walks its track list the same way when it renumbers the index hints
		// it sends an OVT1 edge, which it does after rebuilding the renditions;
		// changing this order would misplace every hint an OVT1 edge receives.
		std::set<uint32_t> described_ids;
		std::set<uint32_t> registered_ids;
		std::vector<ov::String> late_tracks;
		for (const auto &new_track : _described_tracks)
		{
			described_ids.insert(new_track->GetId());

			if (allowed_track_ids.has_value() && (allowed_track_ids->count(new_track->GetId()) == 0))
			{
				logti("[%s/%s(%u)] Track(%u) was described but is not in the play response's allowedTrackIds; not registered",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), new_track->GetId());
				continue;
			}

			// The first describe registers the track; a re-describe replaces it
			// with the next version. Both register the config hint so the router
			// adopts the described values (e.g. Opus parameters not in the bitstream).
			//
			// A track id the first describe did not carry is named rather than added.
			// `info::Stream` orders its track containers at setup and the readers load slots without
			// a lock, so adding one here would restructure them while the media path walks them.
			// No consumer would see it either: they copied this stream when it was created.
			if (GetTrack(new_track->GetId()) == nullptr)
			{
				if (_track_layout_fixed)
				{
					late_tracks.push_back(ov::String::FormatString("%u", new_track->GetId()));
					continue;
				}

				AddTrack(new_track);
				UpdatePacketConfigHint(new_track);
			}
			else if (ChangeTrack(new_track) == false)
			{
				return false;
			}

			registered_ids.insert(new_track->GetId());
		}
		_described_tracks.clear();

		// A track an earlier describe registered but this one does not carry stays on this object,
		// for the same reason a late one cannot be added: `RemoveTrack()` is a setup-only entry point.
		// The track then receives no media, and the rule that the registered set is a subset of
		// the play set does not hold across a failover re-describe.
		// It is named here because this is the moment it becomes a ghost, and nothing downstream can
		// tell it apart from a track that is merely quiet.
		std::vector<ov::String> ghosts;
		for (const auto &[track_id, track] : GetTracks())
		{
			if (described_ids.count(track_id) == 0)
			{
				ghosts.push_back(ov::String::FormatString("%u", track_id));
			}
		}

		if (ghosts.empty() == false)
		{
			logtw(
				"[%s/%s(%u)] %zu track(s) stay registered without the origin describing them: %s. "
				"They receive no media and cannot be removed while the stream is shared",
				GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
				ghosts.size(), ov::String::Join(ghosts, ", ").CStr());
		}

		if (late_tracks.empty() == false)
		{
			logtw(
				"[%s/%s(%u)] %zu track(s) the origin describes now were not in this stream's first "
				"describe and are not registered: %s. The track layout is settled by the first play response "
				"and cannot grow, because `info::Stream` fixes its track containers before the stream is shared",
				GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
				late_tracks.size(), ov::String::Join(late_tracks, ", ").CStr());
		}

		if (registered_ids.empty())
		{
			// Two different causes end up here, and the operator cannot tell them apart from the counts alone.
			if (late_tracks.empty() == false)
			{
				logte(
					"[%s/%s(%u)] None of the %zu track(s) this origin describes matches the track layout this "
					"stream settled on, so it has nothing to deliver. This origin cannot serve the stream; "
					"the pull provider moves on to the next URL, or terminates the stream when `RetryCount` is 0",
					GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), late_tracks.size());
			}
			else
			{
				logte("[%s/%s(%u)] No track is left to register after the play response",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			}

			return false;
		}

		// An OVT1 origin sends renditions with index hints and no track ids, so nothing downstream can
		// pin them to a track. Resolve them here against this stream's tracks. Relaying is what needs
		// it: `RenumberIndexHints()` rewrites a side only when it carries an id, and would otherwise
		// leave one pointing at the pre-filter group. An audio side whose hint is -1 keeps its
		// whole-group meaning and stays id-less, which is what the hint asks for.
		// An OVT2 origin already sent its own ids and they are authoritative,
		// so this runs only for an OVT1 one and never overwrites them.
		if (connection->origin_is_ovt2 == false)
		{
			for (const auto &[file_name, playlist] : GetPlaylists())
			{
				ResolveRenditionTrackIds(std::const_pointer_cast<info::Playlist>(playlist));
			}
		}

		_track_layout_fixed = true;

		return true;
	}

	bool OvtStream::ParsePlaylists(const Json::Value &json_playlists, std::vector<std::shared_ptr<info::Playlist>> &out, const char *source)
	{
		out.clear();
		for (size_t i = 0; i < json_playlists.size(); i++)
		{
			auto json_playlist = json_playlists[static_cast<int>(i)];

			// Validate
			if (!json_playlist["name"].isString() || !json_playlist["fileName"].isString() ||
				!json_playlist["options"].isObject() || !json_playlist["renditions"].isArray())
			{
				logte("Invalid json payload : playlist");
				return false;
			}

			ov::String playlist_name = json_playlist["name"].asString().c_str();
			ov::String playlist_file_name = json_playlist["fileName"].asString().c_str();

			auto playlist = std::make_shared<info::Playlist>(playlist_name, playlist_file_name, false);
			if (json_playlist["enableSubtitles"].isBool())
			{
				playlist->EnableSubtitles(json_playlist["enableSubtitles"].asBool());
			}

			// Options
			auto json_options = json_playlist["options"];

			if (json_options.isNull() == false)
			{
				// Validate
				if (json_options["webrtcAutoAbr"].isBool())
				{
					playlist->SetWebRtcAutoAbr(json_options["webrtcAutoAbr"].asBool());
				}

				if (json_options["hlsChunklistPathDepth"].isInt())
				{
					playlist->SetHlsChunklistPathDepth(json_options["hlsChunklistPathDepth"].asInt());
				}

				if (json_options["enableTsPackaging"].isBool())
				{
					playlist->EnableTsPackaging(json_options["enableTsPackaging"].asBool());
				}
			}

			for (size_t j = 0; j < json_playlist["renditions"].size(); j++)
			{
				auto json_rendition = json_playlist["renditions"][static_cast<int>(j)];

				// Validate
				if (!json_rendition["name"].isString() || !json_rendition["videoTrackName"].isString() || !json_rendition["audioTrackName"].isString())
				{
					logte("Invalid json payload : playlist rendition");
					return false;
				}

				ov::String rendition_name = json_rendition["name"].asString().c_str();
				ov::String video_track_name = json_rendition["videoTrackName"].asString().c_str();
				ov::String audio_track_name = json_rendition["audioTrackName"].asString().c_str();
			
				int video_index_hint = -1;
				int audio_index_hint = -1;

				if (json_rendition["videoIndexHint"].isInt())
				{
					video_index_hint = json_rendition["videoIndexHint"].asInt();
				}

				if (json_rendition["audioIndexHint"].isInt())
				{
					audio_index_hint = json_rendition["audioIndexHint"].asInt();
				}
			
				auto rendition = std::make_shared<info::Rendition>(rendition_name, video_track_name, audio_track_name);
				rendition->SetVideoIndexHint(video_index_hint);
				rendition->SetAudioIndexHint(audio_index_hint);

				// OVT2 track references; the consumers confirm them against the registered tracks before
				// use. An absent key is an OVT1 origin or a side the origin could not pin; a present key
				// of the wrong type is a broken contract and is refused like any other bad field.
				auto optional_track_id = [&](const char *key, bool ok) {
					if ((json_rendition[key].isNull() == false) && (ok == false))
					{
						logte("Invalid json payload : playlist rendition field `%s`", key);
						return false;
					}

					return true;
				};

				if ((optional_track_id("videoTrackId", json_rendition["videoTrackId"].isUInt()) == false) ||
					(optional_track_id("audioTrackId", json_rendition["audioTrackId"].isUInt()) == false) ||
					(optional_track_id("removedVideoTrackName", json_rendition["removedVideoTrackName"].isString()) == false) ||
					(optional_track_id("removedAudioTrackName", json_rendition["removedAudioTrackName"].isString()) == false))
				{
					return false;
				}

				if (json_rendition["videoTrackId"].isUInt())
				{
					rendition->SetVideoTrackId(json_rendition["videoTrackId"].asUInt());
				}
				if (json_rendition["audioTrackId"].isUInt())
				{
					rendition->SetAudioTrackId(json_rendition["audioTrackId"].asUInt());
				}
			
				// The origin names the variant it had to drop from this side. The rendition is kept as it
				// arrived, because the origin already decided it is worth sending; this only says why it
				// is one-sided, which the empty name alone cannot. The origin stamps the marker for any
				// reason it could not fill the side, so the cause is left to the origin's own log.
				auto removed_side = [&](const char *key, const char *side) {
					if (json_rendition[key].isString() == false)
					{
						return;
					}

					logtw("[%s/%s(%u)] Rendition [%s] of playlist [%s] in the %s response has no %s side: the origin has no track left for variant [%s]",
						  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
						  rendition_name.CStr(), playlist_file_name.CStr(), source, side,
						  ovt::SanitizeForLog(json_rendition[key].asCString()).CStr());
				};
				removed_side("removedVideoTrackName", "video");
				removed_side("removedAudioTrackName", "audio");

				playlist->AddRendition(rendition);
			}

			logti("Playlist from the %s response: %s", source, playlist->ToString().CStr());

			out.push_back(playlist);
		}

		return true;
	}

	bool OvtStream::ReceiveDescribe(const std::shared_ptr<Connection> &connection, uint32_t request_id)
	{
		auto data = ReceiveMessage(connection);
		if (data == nullptr || data->GetLength() <= 0)
		{
			return false;
		}

		// Parsing Payload
		ov::String payload(data->GetDataAs<char>(), data->GetLength());
		ov::JsonObject object = ov::Json::Parse(payload);

		if (object.IsNull() || (object.GetJsonValue().isObject() == false))
		{
			logte("An invalid response : Json format");
			return false;
		}

		// Every `ovt` read below goes through this reference.
		// `GetJsonValue()` returns a non-const value, whose subscript creates the key instead of reading it
		// and throws when the value is not an object.
		const Json::Value &root = object.GetJsonValue();

		// The peer generation comes first, and the `ovt` object alone decides it.
		switch (ovt::JudgeOriginResponse(root))
		{
			case ovt::OriginVerdict::NotAnObject:
				logte("%s/%s(%u) - Origin filled ovt with something other than an object",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
				return false;

			case ovt::OriginVerdict::NoMediaHeaderVersion:
				logte("%s/%s(%u) - Origin sent an ovt object without a readable mediaHeaderVersion",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
				return false;

			case ovt::OriginVerdict::MediaHeaderTooNew:
				logte("%s/%s(%u) - Origin writes media header layout %u, this build reads up to %u",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
					  ovt::GetMediaHeaderVersion(root).value_or(0), ovt::OVT_MEDIA_HEADER_VERSION);
				return false;

			case ovt::OriginVerdict::Ovt1:
				connection->origin_is_ovt2 = false;
				break;

			case ovt::OriginVerdict::Ovt2:
				connection->origin_is_ovt2 = true;
				// Kept, never judged on. A later release reads it to hold back something this origin
				// would mishandle; refusing on it would let the older side decide for the newer one.
				connection->origin_version = ovt::GetPeerVersion(root);
				break;
		}

		{
			// The origin's side of what the publisher logs for an edge. One line per connection,
			// because a connection describes once. Only this line reads the agent,
			// so it does not outlive the response.
			auto agent = (connection->origin_is_ovt2 && root["ovt"]["agent"].isString())
							 ? ovt::SanitizeForLog(root["ovt"]["agent"].asString().c_str())
							 : ov::String();

			logti("%s/%s(%u) - Origin %s speaks %s (generation %u)%s%s",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
				  _curr_url != nullptr ? _curr_url->ToUrlString().CStr() : "<unknown>",
				  connection->origin_is_ovt2 ? "OVT2" : "OVT1", connection->origin_version,
				  agent.IsEmpty() ? "" : ", agent: ", agent.CStr());
		}

		// `ovt.required`: one token this build does not know refuses the whole stream.
		// Tokens are compared as they are against the supported set;
		// nothing is split or case-folded.
		if (connection->origin_is_ovt2 && CheckRequiredTokens(root["ovt"]["required"], "describe") == false)
		{
			return false;
		}

		// `ovt.reason`: the origin answers 200 with an empty track set for OVT1 compatibility,
		// so this token is the only way to tell "nothing to deliver" from a normal response.
		const Json::Value &json_reason = root["ovt"]["reason"];
		if (connection->origin_is_ovt2 && json_reason.isString() &&
			(ov::String(json_reason.asCString()) == ovt::REASON_NO_DELIVERABLE_TRACK))
		{
			logte("%s/%s(%u) - Origin has no track this edge can receive",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());

			return false;
		}

		Json::Value &json_id = object.GetJsonValue()["id"];
		Json::Value &json_application = object.GetJsonValue()["application"];
		Json::Value &json_code = object.GetJsonValue()["code"];
		Json::Value &json_message = object.GetJsonValue()["message"];
		Json::Value &json_contents = object.GetJsonValue()["contents"];

		if (!json_id.isUInt() || json_application.isNull() || !json_code.isUInt() || json_message.isNull())
		{
			logte("An invalid response : There are no required keys");
			return false;
		}

		if (request_id != json_id.asUInt())
		{
			logte("An invalid response : Response ID is wrong. (%d / %d)", request_id, json_id.asUInt());
			return false;
		}

		if (json_code.asUInt() != 200)
		{
			logte("Describe : Server Failure : %d (%s)", json_code.asUInt(), json_message.asString().c_str());
			return false;
		}

		ov::String application = json_application.asString().c_str();
		if (ovt::IsApplication(application, ovt::APPLICATION_DESCRIBE) == false)
		{
			logte("An invalid response : wrong application : %s", application.CStr());
			return false;
		}

		if (json_contents.isNull())
		{
			logte("An invalid response : There is no contents");
			return false;
		}

		// Parse stream and add track
		auto json_version = json_contents["version"];
		auto json_stream = json_contents["stream"];
		auto json_tracks = json_stream["tracks"];
		auto json_playlists = json_stream["playlists"];

		// Validation
		if (json_version.isUInt() == false)
		{
			logte("Invalid json payload : version");
			return false;
		}

		// renditions is optional
		if (!json_stream["appName"].isString() || !json_stream["streamName"].isString() || json_stream["tracks"].isNull() ||
			!json_tracks.isArray())
		{
			logte("Invalid json payload : stream");
			return false;
		}

		// `playlists` is an array. Legacy compatibility: `v0.21.0.0` sends null instead when the stream
		// has none, so that one shape is normalized here and nothing below sees anything but an array.
		// Drop this branch only once no `v0.21.0.0` origin can be reached.
		if (json_playlists.isNull())
		{
			json_playlists = Json::Value(Json::arrayValue);
		}
		else if (json_playlists.isArray() == false)
		{
			logte("Invalid json payload : playlists");
			return false;
		}

		// Latest version origin server sends UUID of origin stream
		if (json_stream["originStreamUUID"].isString())
		{
			SetOriginStreamUUID(json_stream["originStreamUUID"].asString().c_str());
		}

		// Renditions. Whatever an earlier attempt parsed is discarded: only this describe's playlists count
		if (ParsePlaylists(json_playlists, _described_playlists, "describe") == false)
		{
			return false;
		}

		//SetName(json_stream["streamName"].asString().c_str());
		_described_tracks.clear();
		for (size_t i = 0; i < json_tracks.size(); i++)
		{
			auto new_track = ParseTrackFromJson(connection, json_tracks[static_cast<int>(i)]);
			if (new_track == nullptr)
			{
				logte("Invalid json track [%zu]", i);
				return false;
			}

			// A track this build cannot carry is filtered here, before registration.
			// Once registered, its packets would be dropped downstream:
			// the LLHLS readiness check would stay false for good,
			// and the mediarouter logs an error for every packet of a track it cannot handle.
			if (connection->origin_is_ovt2)
			{
				const auto &json_track = json_tracks[static_cast<int>(i)];
				auto skip_reason = GetTrackSkipReason(json_track, new_track);
				if (skip_reason.has_value())
				{
					logtw("[%s/%s(%u)] Track(%u, codec: %s) from the origin is not registered: %s",
						  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
						  new_track->GetId(), json_track["codec"].isString() ? json_track["codec"].asCString() : "-", skip_reason->CStr());
					continue;
				}
			}

			// Registration waits for the play response, which names the tracks the origin will send
			_described_tracks.push_back(new_track);
		}

		// logti("[%s/%s(%u)] stream has been described . %s", GetApplicationTypeName(), GetName().CStr(), GetId(), payload.CStr());

		SetState(State::DESCRIBED);
		return true;
	}

	// A value this build does not know is tolerated; a value of the wrong type is not.
	// The first is forward compatibility, the second says the peer broke the contract, and going on
	// would mean playing a track whose real configuration is unknown.
	//
	// Absence is not a violation for a field a later release added, because every older origin omits it
	// and the reader takes the absent value as zero. jsoncpp's type predicates are false for an absent
	// key, so such a field has to be spelled with `optional_*` or the omission reads as a broken value.
	std::vector<ov::String> OvtStream::InvalidTrackFields(const Json::Value &json_track)
	{
		std::vector<ov::String> invalid;

		auto require = [&](const char *field, bool ok) {
			if (ok == false)
			{
				invalid.emplace_back(field);
			}
		};

		// A field some release this build talks to does not send. Absent is fine; present with the
		// wrong type is not. `key` is the JSON key inside `parent`, and `prefix` puts the sub-object
		// name in front of it for the log, so the key stays the one actually looked up.
		auto optional_field = [&](const Json::Value &parent, const char *prefix, const char *key, bool ok) {
			if ((parent[key].isNull() == false) && (ok == false))
			{
				invalid.emplace_back(ov::String::FormatString("%s%s", prefix, key));
			}
		};

		require("id", json_track["id"].isUInt());
		require("name", json_track["name"].isString());
		require("codecId", json_track["codecId"].isUInt());
		require("mediaType", json_track["mediaType"].isUInt());
		require("timebaseNum", json_track["timebaseNum"].isUInt());
		require("timebaseDen", json_track["timebaseDen"].isUInt());
		require("bitrate", json_track["bitrate"].isUInt());
		require("startFrameTime", json_track["startFrameTime"].isUInt64());
		require("lastFrameTime", json_track["lastFrameTime"].isUInt64());

		// `decoderConfig` from v0.15.4, the three labels from v0.20.5
		optional_field(json_track, "", "publicName", json_track["publicName"].isString());
		optional_field(json_track, "", "language", json_track["language"].isString());
		optional_field(json_track, "", "characteristics", json_track["characteristics"].isString());
		optional_field(json_track, "", "decoderConfig", json_track["decoderConfig"].isString());

		// OVT2 only, and these decide the configuration the track is played with,
		// so a wrong type here is the case this function exists for.
		optional_field(json_track, "", "codec", json_track["codec"].isString());
		optional_field(json_track, "", "mediaTypeName", json_track["mediaTypeName"].isString());
		optional_field(json_track, "", "codecs", json_track["codecs"].isString());

		// The media type is not final until the OVT2 names are read, so both sub-objects are checked
		// whenever they are there. Only the one matching the final type is read afterwards.
		const auto &json_video_track = json_track["videoTrack"];
		if (json_video_track.isNull() == false)
		{
			require("videoTrack.framerate", json_video_track["framerate"].isNumeric());
			require("videoTrack.width", json_video_track["width"].isUInt());
			require("videoTrack.height", json_video_track["height"].isUInt());

			// Added in v0.20.5
			optional_field(json_video_track, "videoTrack.", "maxFramerate", json_video_track["maxFramerate"].isNumeric());
			optional_field(json_video_track, "videoTrack.", "maxWidth", json_video_track["maxWidth"].isUInt());
			optional_field(json_video_track, "videoTrack.", "maxHeight", json_video_track["maxHeight"].isUInt());
		}

		const auto &json_audio_track = json_track["audioTrack"];
		if (json_audio_track.isNull() == false)
		{
			require("audioTrack.samplerate", json_audio_track["samplerate"].isUInt());
			require("audioTrack.sampleFormat", json_audio_track["sampleFormat"].isInt());
			require("audioTrack.layout", json_audio_track["layout"].isUInt());

			// OVT2 only
			optional_field(json_audio_track, "audioTrack.", "sampleFormatName", json_audio_track["sampleFormatName"].isString());
		}

		return invalid;
	}

	// Both ends derive `codecs` from the decoder configuration record the describe carries,
	// so the strings agreeing is the check that the two builds read those bytes the same way.
	// Nothing is taken from the received one: what the playlists use is always the local derivation.
	// A side with nothing to offer ends the comparison: the origin omits the key whenever it could not
	// derive one, and this build compares only what it derived itself.
	bool OvtStream::CodecsDisagree(const ov::String &derived, const Json::Value &json_codecs)
	{
		if ((derived.IsEmpty()) || (json_codecs.isString() == false))
		{
			return false;
		}

		return derived != ov::String(json_codecs.asCString());
	}

	// Every bad field is named so the operator does not have to read it out of a parser message.
	bool OvtStream::ValidateTrackJson(const Json::Value &json_track)
	{
		auto invalid = InvalidTrackFields(json_track);

		for (const auto &field : invalid)
		{
			logte("[%s/%s(%u)] Track field `%s` has the wrong type", GetApplicationInfo().GetVHostAppName().CStr(),
				  GetName().CStr(), GetId(), field.CStr());
		}

		return invalid.empty();
	}

	std::shared_ptr<MediaTrack> OvtStream::ParseTrackFromJson(const std::shared_ptr<Connection> &connection, const Json::Value &json_track)
	{
		if (ValidateTrackJson(json_track) == false)
		{
			return nullptr;
		}

		auto new_track = std::make_shared<MediaTrack>();

		new_track->SetId(json_track["id"].asUInt());
		new_track->SetVariantName(json_track["name"].asString().c_str());
		new_track->SetPublicName(json_track["publicName"].asString().c_str());
		new_track->SetLanguage(json_track["language"].asString().c_str());
		new_track->SetCharacteristics(json_track["characteristics"].asString().c_str());
		// Receive-side boundary of the OVT wire table (`ovt_wire.h`);
		// the send side is the OVT publisher's `GenerateTrackDescription()`.
		// An integer without a table entry leaves the enum at `None`/`Unknown` instead of being cast into it.
		auto wire_codec_id = ovt::FromOvtWireInt<cmn::MediaCodecId>(json_track["codecId"].asUInt()).value_or(cmn::MediaCodecId::None);
		auto wire_media_type = ovt::FromOvtWireInt<cmn::MediaType>(json_track["mediaType"].asUInt()).value_or(cmn::MediaType::Unknown);
		new_track->SetCodecId(wire_codec_id);
		new_track->SetMediaType(wire_media_type);

		// From an OVT2 origin the names decide; the integers and the `codecs` string are cross-checked
		// against them, and a track that describes itself inconsistently is reported once whatever disagrees.
		// Names this build does not know leave `None`/`Unknown`,
		// and the caller decides whether such a track is registered (`GetTrackSkipReason()`),
		// or, for a notification, applied as it is.
		ov::String description_mismatch;
		if (connection->origin_is_ovt2)
		{
			if (json_track["codec"].isString())
			{
				new_track->SetCodecId(cmn::GetCodecIdByExactName(json_track["codec"].asString().c_str()).value_or(cmn::MediaCodecId::None));
			}
			if (json_track["mediaTypeName"].isString())
			{
				new_track->SetMediaType(cmn::GetMediaTypeByName(json_track["mediaTypeName"].asString().c_str()).value_or(cmn::MediaType::Unknown));
			}

			if ((new_track->GetCodecId() != wire_codec_id) || (new_track->GetMediaType() != wire_media_type))
			{
				description_mismatch.AppendFormat(
					"names (%s / %s) disagree with the integers (%s / %s), and the names are used",
					cmn::GetCodecIdString(new_track->GetCodecId()), cmn::GetMediaTypeString(new_track->GetMediaType()),
					cmn::GetCodecIdString(wire_codec_id), cmn::GetMediaTypeString(wire_media_type));
			}
		}

		new_track->SetTimeBase(json_track["timebaseNum"].asUInt(), json_track["timebaseDen"].asUInt());
		new_track->SetBitrateByConfig(json_track["bitrate"].asUInt());

		// video or audio
		if (new_track->GetMediaType() == cmn::MediaType::Video)
		{
			auto json_video_track = json_track["videoTrack"];
			if (json_video_track.isNull())
			{
				logtd("Invalid json videoTrack");
				return nullptr;
			}

			new_track->SetFrameRateByConfig(json_video_track["framerate"].asDouble());
			new_track->SetMaxFrameRate(json_video_track["maxFramerate"].asDouble());
			new_track->SetResolution(json_video_track["width"].asUInt(), json_video_track["height"].asUInt());
			new_track->SetMaxResolution(json_video_track["maxWidth"].asUInt(), json_video_track["maxHeight"].asUInt());
		}
		else if (new_track->GetMediaType() == cmn::MediaType::Audio)
		{
			auto json_audio_track = json_track["audioTrack"];
			if (json_audio_track.isNull())
			{
				logtd("Invalid json audioTrack");
				return nullptr;
			}

			new_track->SetSampleRate(json_audio_track["samplerate"].asUInt());
			if (new_track->GetSampleRate() == 0)
			{
				logte("Audio track(%u) received from origin has samplerate=0. The origin may have sent an invalid AudioSpecificConfig.", new_track->GetId());
			}
			auto wire_sample_format = ovt::FromOvtWireInt<cmn::AudioSample::Format>(json_audio_track["sampleFormat"].asInt()).value_or(cmn::AudioSample::Format::None);
			auto sample_format		= wire_sample_format;
			if (connection->origin_is_ovt2 && json_audio_track["sampleFormatName"].isString())
			{
				sample_format = cmn::GetAudioSampleFormatByName(json_audio_track["sampleFormatName"].asString().c_str()).value_or(cmn::AudioSample::Format::None);
			}
			new_track->SetSampleFormat(sample_format);

			if (sample_format != wire_sample_format)
			{
				if (description_mismatch.IsEmpty() == false)
				{
					description_mismatch.Append(", and ");
				}

				description_mismatch.AppendFormat("sample format name (%s) disagrees with the integer (%s), and the name is used",
												  new_track->GetSample().GetName(), cmn::AudioSample(wire_sample_format).GetName());
			}
			// The layout value is the OR of its channel bits, so there is no wire table to go through,
			// only the question of whether this build defines that combination.
			// One it does not is left `LayoutUnknown` and kept as received, so a relay hands the next hop
			// the original: the value may be a layout a newer build added.
			auto wire_layout = json_audio_track["layout"].asUInt();
			auto layout = cmn::AudioChannel::GetLayoutByValue(wire_layout);
			new_track->SetChannelLayout(layout.value_or(cmn::AudioChannel::Layout::LayoutUnknown));
			new_track->SetUnmappedChannelLayout(
				layout.has_value() ? std::nullopt : std::optional<uint32_t>(wire_layout));

			if (layout.has_value() == false)
			{
				logtw("[%s/%s(%u)] Track(%u) carries channel layout %u, which this build does not define",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), new_track->GetId(), wire_layout);
			}
		}

		auto decoder_config = json_track["decoderConfig"];
		if (decoder_config.isString())
		{
			auto config_data = ov::Base64::Decode(decoder_config.asString().c_str());
			auto decoder_config_record = DecoderConfigurationRecordParser::Parse(new_track->GetCodecId(), config_data);
			new_track->SetDecoderConfigurationRecord(decoder_config_record);
		}

		if (connection->origin_is_ovt2 && CodecsDisagree(new_track->GetCodecsParameter(), json_track["codecs"]))
		{
			if (description_mismatch.IsEmpty() == false)
			{
				description_mismatch.Append(", and ");
			}

			description_mismatch.AppendFormat("`codecs` reads \"%s\" where this build derives \"%s\"",
											  ovt::SanitizeForLog(json_track["codecs"].asCString()).CStr(),
											  new_track->GetCodecsParameter().CStr());
		}

		if (description_mismatch.IsEmpty() == false)
		{
			// The handshake and the NOTIFY path both reach here, so the set is locked.
			// The ids come from the origin, so the set is capped the same way as the other two:
			// a NOTIFY that varies the id every time would otherwise grow it without end.
			std::lock_guard<std::mutex> lock(connection->describe_mismatch_lock);
			auto &logged = connection->describe_mismatch_logged;
			if ((logged.size() < MAX_LOGGED_TRACK_IDS) && logged.insert(new_track->GetId()).second)
			{
				logtw("[%s/%s(%u)] Track(%u) describes itself inconsistently: %s",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
					  new_track->GetId(), description_mismatch.CStr());

				if (logged.size() == MAX_LOGGED_TRACK_IDS)
				{
					logtw("[%s/%s(%u)] %zu tracks reported so far; no more will be listed for this connection",
						  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), logged.size());
				}
			}
		}

		return new_track;
	}

	bool OvtStream::ApplyTrackNotification(const std::shared_ptr<Connection> &connection, const Json::Value &contents)
	{
		auto json_tracks = contents["stream"]["tracks"];
		if (json_tracks.isArray() == false)
		{
			logte("[%s/%s(%u)] Received a track change notification without a valid tracks array",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			return false;
		}

		for (size_t i = 0; i < json_tracks.size(); i++)
		{
			auto new_track = ParseTrackFromJson(connection, json_tracks[static_cast<int>(i)]);
			if (new_track == nullptr)
			{
				// Skipping the track would leave it on its old configuration while the packets that
				// follow carry the new one, which is the silent misreading this protocol exists to stop.
				// The notification names no other way to learn what changed, so the stream ends here.
				logte("[%s/%s(%u)] A track in the change notification is malformed; ending the stream",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
				return false;
			}

			// Only replace tracks this stream actually subscribed to. A TrackSet
			// edge legitimately omits tracks the origin still notifies about.
			if (GetTrack(new_track->GetId()) == nullptr)
			{
				continue;
			}

			if (ChangeTrack(new_track) == false)
			{
				continue;
			}

			// A change to a codec this build does not know is applied as it is and only logged.
			// The OVT layer drops nothing; what happens to that track is up to its consumers,
			// and the start gates are start-only (sticky),
			// so a mid-stream change never stops the whole stream.
			const auto &json_track = json_tracks[static_cast<int>(i)];
			bool unknown_codec = connection->origin_is_ovt2
									 ? (json_track["codec"].isString() == false || cmn::GetCodecIdByExactName(json_track["codec"].asString().c_str()).has_value() == false)
									 : (ovt::FromOvtWireInt<cmn::MediaCodecId>(json_track["codecId"].asUInt()).has_value() == false);
			if (unknown_codec)
			{
				logtw("[%s/%s(%u)] Origin changed track(%u) to a codec this build does not know (codec: %s, codecId: %u)",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), new_track->GetId(),
					  json_track["codec"].isString() ? json_track["codec"].asCString() : "-", json_track["codecId"].asUInt());
			}

			logti("[%s/%s(%u)] Applied origin track(%u) configuration change (version %u)",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
				  new_track->GetId(), new_track->GetVersion());
		}

		return true;
	}

	bool OvtStream::RequestPlay(const std::shared_ptr<Connection> &connection)
	{
		if (GetState() != State::DESCRIBED)
		{
			logte("%s/%s(%u) - Could not request to play. Before receiving describe.", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			return false;
		}

		Json::Value root;
		_last_request_id++;
		root["id"] = _last_request_id;
		root["application"] = ovt::APPLICATION_PLAY;
		root["target"] = _curr_url->Source().CStr();
		root["ovt"] = ovt::MakeRequestOvtObject();

		// The tracks this edge will register. `_described_tracks` is what the describe left after the
		// skip rules ran, so a track this build cannot carry is never asked for and the origin stops
		// building renditions around it. The skip rules read the OVT2 name fields, so they only run for
		// an OVT2 origin; from an OVT1 one every described track is registered and the key is not sent.
		if (connection->origin_is_ovt2)
		{
			Json::Value track_ids(Json::arrayValue);
			for (const auto &track : _described_tracks)
			{
				track_ids.append(track->GetId());
			}
			root["ovt"]["trackIds"] = track_ids;
		}

		auto message = ov::Json::Stringify(root).ToData(false);

		std::shared_lock<std::shared_mutex> lock(_packetizer_lock);
		if (_packetizer->PacketizeMessage(OvtPayloadType::MessageRequest, ov::Clock::NowMSec(), message) == false)
		{
			logte("%s/%s(%u) - Could not request to play. Socket send error", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			return false;
		}

		try
		{
			return ReceivePlay(connection, _last_request_id);
		}
		catch (const Json::Exception &e)
		{
			logte("%s/%s(%u) - An invalid play response : %s", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), e.what());
			return false;
		}
	}

	bool OvtStream::ReceivePlay(const std::shared_ptr<Connection> &connection, uint32_t request_id)
	{
		auto message = ReceiveMessage(connection);
		if (message == nullptr)
		{
			logte("%s/%s(%u) - Could not receive message", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			return false;
		}

		// Parsing Payload
		ov::String payload(message->GetDataAs<char>(), message->GetLength());
		ov::JsonObject object = ov::Json::Parse(payload);

		if (object.IsNull() || (object.GetJsonValue().isObject() == false))
		{
			logte("An invalid response : Json format");
			return false;
		}

		// Each response is judged on its own, so the play response goes through the same `ovt` rule as
		// the describe one, and every `ovt` read below goes through this reference for the same reason.
		// The generation itself is settled by the describe: an origin that answers the two in different
		// generations, or changes its media header layout between them, is refused rather than served
		// half under each rule.
		const Json::Value &root = object.GetJsonValue();

		switch (ovt::JudgeOriginResponse(root))
		{
			case ovt::OriginVerdict::NotAnObject:
				logte("%s/%s(%u) - Origin filled ovt with something other than an object",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
				return false;

			case ovt::OriginVerdict::NoMediaHeaderVersion:
				logte("%s/%s(%u) - Origin answered play with an ovt object without a readable mediaHeaderVersion",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
				return false;

			case ovt::OriginVerdict::MediaHeaderTooNew:
				logte("%s/%s(%u) - Origin answers play with media header layout %u, this build reads up to %u",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
					  ovt::GetMediaHeaderVersion(root).value_or(0), ovt::OVT_MEDIA_HEADER_VERSION);
				return false;

			case ovt::OriginVerdict::Ovt1:
				if (connection->origin_is_ovt2)
				{
					logte("%s/%s(%u) - Origin described as OVT2 and answered play as OVT1",
						  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
					return false;
				}
				break;

			case ovt::OriginVerdict::Ovt2:
				if (connection->origin_is_ovt2 == false)
				{
					logte("%s/%s(%u) - Origin described as OVT1 and answered play as OVT2",
						  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
					return false;
				}
				break;
		}

		Json::Value &json_id = object.GetJsonValue()["id"];
		Json::Value &json_app = object.GetJsonValue()["application"];
		Json::Value &json_code = object.GetJsonValue()["code"];
		Json::Value &json_message = object.GetJsonValue()["message"];

		if (!json_id.isUInt() || json_app.isNull() || !json_code.isUInt() || json_message.isNull())
		{
			logte("An invalid response : There are no required keys");
			return false;
		}

		ov::String application = json_app.asString().c_str();

		if (ovt::IsApplication(application, ovt::APPLICATION_PLAY) == false)
		{
			logte("An invalid response : application is wrong (%s).", application.CStr());
			return false;
		}

		if (request_id != json_id.asUInt())
		{
			logte("An invalid response : Response ID is wrong.");
			return false;
		}

		if (json_code.asUInt() != 200)
		{
			logte("Play : Server Failure : %d (%s)", json_code.asUInt(), json_message.asString().c_str());
			return false;
		}

		// `ovt.reason`: describe and play select independently, so a track set that was not empty
		// at describe can be empty here. The token is what tells that apart from a normal response.
		const Json::Value &json_reason = root["ovt"]["reason"];
		if (connection->origin_is_ovt2 && json_reason.isString() &&
			(ov::String(json_reason.asCString()) == ovt::REASON_NO_DELIVERABLE_TRACK))
		{
			logte("%s/%s(%u) - Origin has no track this session can receive",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());

			return false;
		}

		// An OVT2 origin names the tracks this session will receive; only those are registered,
		// so the registered set is always a subset of what arrives.
		// An OVT1 origin sends what describe listed.
		std::optional<std::set<uint32_t>> allowed_track_ids;
		if (connection->origin_is_ovt2)
		{
			size_t ignored = 0;
			allowed_track_ids = ovt::ParseTrackIdArray(root["ovt"]["allowedTrackIds"], &ignored);

			if (ignored > 0)
			{
				logtw("%s/%s(%u) - Ignored %zu non-integer entries in the play response's allowedTrackIds",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), ignored);
			}

			// An origin that sent an `ovt` object said it speaks OVT2,
			// and OVT2 answers play with this array.
			// Without it nothing states what the origin will send, so registering the described
			// set would be a guess that the subset rule above cannot stand on. The response is refused.
			if (allowed_track_ids.has_value() == false)
			{
				logte("%s/%s(%u) - OVT2 origin answered play without an allowedTrackIds array",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());

				return false;
			}

			// The describe's playlists were built before this edge made its selection, so its renditions
			// may still point at tracks that are not in the confirmed set. This array is the rebuilt one
			// and replaces them. An origin that sends none leaves the describe's copy in place.
			// Unlike the describe field, this one takes no null: it is OVT2-only and no release ever
			// sent it, so there is no legacy shape to accept here.
			const Json::Value &json_playlists = root["ovt"]["playlists"];
			if (json_playlists.isArray())
			{
				if (ParsePlaylists(json_playlists, _described_playlists, "play") == false)
				{
					logte("%s/%s(%u) - Origin answered play with an unreadable playlists array",
						  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
					return false;
				}
			}
		}

		if (RegisterDescribedTracks(connection, allowed_track_ids) == false)
		{
			return false;
		}

		SetState(State::PLAYING);
		return true;
	}

	bool OvtStream::RequestStop()
	{
		if (GetState() != State::PLAYING)
		{
			return false;
		}

		Json::Value root;
		_last_request_id++;
		root["id"] = _last_request_id;
		root["application"] = ovt::APPLICATION_STOP;
		root["target"] = _curr_url->Source().CStr();
		root["ovt"] = ovt::MakeRequestOvtObject();

		auto message = ov::Json::Stringify(root).ToData(false);

		std::shared_lock<std::shared_mutex> lock(_packetizer_lock);
		if (_packetizer->PacketizeMessage(OvtPayloadType::MessageRequest, ov::Clock::NowMSec(), message) == false)
		{
			return false;
		}

		return true;
	}

	bool OvtStream::OnOvtPacketized(std::shared_ptr<OvtPacket> &packet)
	{
		auto client_socket = std::atomic_load(&_connection)->socket;
		if (client_socket == nullptr)
		{
			logte("Could not send message : socket is null");
			return false;
		}

		if (client_socket->Send(packet->GetData()) == false)
		{
			logte("Could not send message");
			return false;
		}

		return true;
	}

	std::shared_ptr<ov::Data> OvtStream::ReceiveMessage(const std::shared_ptr<Connection> &connection)
	{
		// A response is the first message a request gets, so anything else while waiting is a stray.
		// Strays are counted rather than skipped forever: this loop runs on the thread that starts
		// every pull stream of the application, and a peer that keeps sending non-responses
		// would hold that thread for as long as it keeps writing.
		size_t ignored = 0;
		auto too_many_strays = [&]() {
			if (++ignored <= MAX_IGNORED_MESSAGES_WHILE_WAITING)
			{
				return false;
			}

			logte("[%s/%s] Gave up waiting for a response after %zu message(s) of another kind",
				  GetApplicationName(), GetName().CStr(), ignored);
			return true;
		};

		const auto &depacketizer = connection->depacketizer;

		while (true)
		{
			auto result = ReceivePacket(connection);
			if (result == false)
			{
				logte("%s/%s(%u) - Could not receive packet : err(%d)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), static_cast<uint8_t>(result));
				return nullptr;
			}

			// Media cannot arrive before the play response, which the origin sends ahead of
			// `AddSession()`. One at the head of the queue would block every message behind it,
			// so it is dropped and counted like any other stray.
			while (depacketizer->IsAvailableMediaPacket())
			{
				depacketizer->PopMediaPacket();
				logtw("[%s/%s] Dropped a media packet while waiting for a response", GetApplicationName(), GetName().CStr());

				if (too_many_strays())
				{
					return nullptr;
				}
			}

			// Empty while the message is still arriving in fragments
			auto message = depacketizer->PopMessage();
			if (message.has_value() == false)
			{
				continue;
			}

			if (message->payload_type == OvtPayloadType::MessageResponse)
			{
				return message->data;
			}

			// Only a response can answer the pending request; anything else is dropped while waiting
			logtw("[%s/%s] Ignored a message with payload type %u while waiting for a response", GetApplicationName(), GetName().CStr(),
				  ov::ToUnderlyingType(message->payload_type));

			if (too_many_strays())
			{
				return nullptr;
			}
		}

		return nullptr;
	}

	bool OvtStream::ReceivePacket(const std::shared_ptr<Connection> &connection, bool non_block)
	{
		uint8_t buffer[65535];

		const auto &client_socket = connection->socket;
		if (client_socket == nullptr)
		{
			logte("[%s/%s] Could not receive packet : socket is null", GetApplicationName(), GetName().CStr());
			return false;
		}

		auto result = client_socket->Recv(buffer, 65535, non_block);
		if (result.has_value() == false)
		{
			logte("[%s/%s] An error occurred while receiving packet: %s", GetApplicationName(), GetName().CStr(), result.error()->What());
			client_socket->Close();
			return false;
		}

		auto read_bytes = result.value();
		if (read_bytes == 0)
		{
			// No data available right now - retry later. A real error/disconnect arrives
			// as a failed result and is handled above, so `0` is never fatal here.
			return true;
		}

		if (connection->depacketizer->AppendPacket(buffer, read_bytes) == false)
		{
			logte("[%s/%s] An error occurred while parsing packet: Invalid packet", GetApplicationName(), GetName().CStr());
			return false;
		}

		return true;
	}

	int OvtStream::GetFileDescriptorForDetectingEvent()
	{
		auto client_socket = std::atomic_load(&_connection)->socket;
		if (client_socket == nullptr)
		{
			return -1;
		}
		return client_socket->GetNativeHandle();
	}

	PullStream::ProcessMediaResult OvtStream::ProcessMediaPacket()
	{
		// The stream is registered with monitoring only after the first handshake returns
		ReportOriginTimes();

		// One load for the whole call: the socket, the depacketizer and this origin's bookkeeping
		// all come from the same connection even when the next one starts in the middle of it.
		auto connection = std::atomic_load(&_connection);
		const auto &depacketizer = connection->depacketizer;

		// Non block
		auto result = ReceivePacket(connection, true);
		if (result == false)
		{
			logte("%s/%s(%u) - Could not receive packet : err(%d)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), static_cast<uint8_t>(result));
			return ProcessMediaResult::PROCESS_MEDIA_FAILURE;
		}

		// Consume messages and media in on-wire parse order, so an origin-pushed
		// configuration change applies to the packets that follow it and not to the
		// tail of the previous configuration that preceded it on the wire.
		// `IsAvailableMessage()` names the kind at the front of the queue,
		// so the matching pop below always comes back with that item.
		bool processed = false;
		while (depacketizer->IsAvailable())
		{
			if (depacketizer->IsAvailableMessage())
			{
				auto message = depacketizer->PopMessage();
				auto result = ProcessMessage(connection, *message);
				if (result != PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS)
				{
					return result;
				}

				continue;
			}

			auto media_packet = depacketizer->PopMediaPacket();

			// Packets of a track that was not registered are dropped here,
			// ahead of the timestamp bookkeeping, so an unregistered track never becomes a timestamp base;
			// left to the mediarouter, every such packet would produce an error log.
			// The registered tracks are the allowed set.
			if (GetTrack(media_packet->GetTrackId()) == nullptr)
			{
				// The ids come from the origin, so the set that keeps one log line per id is capped:
				// an origin that varies the id every packet would otherwise grow it without end.
				auto &logged = connection->unregistered_track_logged;
				if (logged.size() < MAX_LOGGED_TRACK_IDS)
				{
					if (logged.insert(media_packet->GetTrackId()).second)
					{
						logtw("[%s/%s(%u)] Dropping packets of track(%u), which is not registered on this stream",
							  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), media_packet->GetTrackId());

						if (logged.size() == MAX_LOGGED_TRACK_IDS)
						{
							logtw("[%s/%s(%u)] %zu unregistered tracks named so far; no more will be listed for this connection",
								  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), logged.size());
						}
					}
				}

				continue;
			}

			media_packet->SetPacketType(cmn::PacketType::OVT);
			CheckWireFormat(connection, media_packet);

			int64_t pts = media_packet->GetPts();
			int64_t dts = media_packet->GetDts();
			int64_t duration = media_packet->GetDuration();

			AdjustTimestampByBase(media_packet->GetTrackId(), pts, dts, std::numeric_limits<int64_t>::max(), duration);
			[[maybe_unused]] auto old_pts = media_packet->GetPts();
			[[maybe_unused]] auto old_dts = media_packet->GetDts();

			media_packet->SetPts(pts);
			media_packet->SetDts(dts);
			media_packet->SetDuration(-1); // Duration should be set by MediaRouter again due to the AdjustTimestampByBase

			logtt("[%s/%s(%u)] ProcessMediaPacket : TrackId(%d) ORI_PTS(%" PRId64 ") PTS(%" PRId64 ") ORI_DTS(%" PRId64 ") DTS(%" PRId64 ") Size(%zu)",
				  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
				  media_packet->GetTrackId(), old_pts, media_packet->GetPts(), old_dts, media_packet->GetDts(), media_packet->GetDataLength());

			SendFrame(media_packet);
			processed = true;
		}

		return processed ? PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS
						 : PullStream::ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
	}
}  // namespace pvd
