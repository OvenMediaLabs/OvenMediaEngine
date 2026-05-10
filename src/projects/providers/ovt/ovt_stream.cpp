//
// Created by getroot on 19. 12. 9.
//

#include "ovt_stream.h"

#include <base/info/playlist_file.h>
#include <modules/bitstream/decoder_configuration_record_parser.h>
#include <modules/ovt_packetizer/ovt_signaling.h>

#include "base/info/application.h"
#include "ovt_provider.h"

#define OV_LOG_TAG "OvtStream"

namespace pvd
{
	namespace
	{
		std::set<int32_t> CollectTrackIds(const info::Stream &stream)
		{
			std::set<int32_t> track_ids;
			for (const auto &[track_id, track] : stream.GetTracks())
			{
				(void)track;
				track_ids.emplace(track_id);
			}

			return track_ids;
		}

		std::set<int32_t> ResolvePlaylistTrackIds(const info::Stream &stream, const std::shared_ptr<const info::Playlist> &playlist)
		{
			std::set<int32_t> track_ids;

			for (const auto &rendition : playlist->GetRenditionList())
			{
				auto video_index_hint = rendition->GetVideoIndexHint();
				if (video_index_hint < 0)
				{
					video_index_hint = 0;
				}

				auto audio_index_hint = rendition->GetAudioIndexHint();
				if (audio_index_hint < 0)
				{
					audio_index_hint = 0;
				}

				auto video_track = stream.GetTrackByVariant(rendition->GetVideoVariantName(), static_cast<uint32_t>(video_index_hint));
				if (video_track != nullptr)
				{
					track_ids.emplace(video_track->GetId());
				}

				auto audio_track = stream.GetTrackByVariant(rendition->GetAudioVariantName(), static_cast<uint32_t>(audio_index_hint));
				if (audio_track != nullptr)
				{
					track_ids.emplace(audio_track->GetId());
				}
			}

			return track_ids;
		}

		ovt::CapabilitySupport ParseRuntimeWideningCapability(const Json::Value &json_capabilities, ovt::CapabilitySupport current_state)
		{
			if (json_capabilities.isObject() == false)
			{
				return (current_state == ovt::CapabilitySupport::UNKNOWN)
						   ? ovt::CapabilitySupport::UNSUPPORTED
						   : current_state;
			}

			auto json_runtime_widening = json_capabilities["runtimeWidening"];
			if (json_runtime_widening.isBool() == false)
			{
				return (current_state == ovt::CapabilitySupport::UNKNOWN)
						   ? ovt::CapabilitySupport::UNSUPPORTED
						   : current_state;
			}

			return json_runtime_widening.asBool() ? ovt::CapabilitySupport::SUPPORTED : ovt::CapabilitySupport::UNSUPPORTED;
		}

		bool TryNormalizeAuthoritativeScopeUrl(const ov::Url *scope_url,
											   const ov::String &normalized_app_name,
											   const ov::String &normalized_stream_name,
											   const ov::String &normalized_stream_path,
											   ov::String &playlist_file_name,
											   bool &is_full_request)
		{
			if (scope_url == nullptr)
			{
				return false;
			}

			if ((scope_url->App() != normalized_app_name) || (scope_url->Stream() != normalized_stream_name))
			{
				return false;
			}

			auto file_name					  = scope_url->File();
			auto resolved_playlist_scope_file = info::GetMasterPlaylistFileName(*scope_url);
			// Non-OVT requests with a non-empty file that is NOT a master playlist
			// (HLS sub-playlists, segments, thumbnails, unknown formats, ...) carry no
			// playlist scope, so they map to a full-stream demand on the upstream.
			if (resolved_playlist_scope_file.IsEmpty() &&
				(scope_url->Scheme().UpperCaseString() != "OVT") &&
				(file_name.IsEmpty() == false))
			{
				playlist_file_name = "";
				is_full_request	   = true;
				return true;
			}

			file_name = resolved_playlist_scope_file;
			if (file_name.IsEmpty())
			{
				if (scope_url->Path() != normalized_stream_path)
				{
					return false;
				}

				playlist_file_name = "";
				is_full_request	   = true;
				return true;
			}

			auto normalized_playlist_path = ov::String::FormatString("%s/%s", normalized_stream_path.CStr(), file_name.CStr());
			if (scope_url->Path() != normalized_playlist_path)
			{
				return false;
			}

			// OME playlist _file_name is stored without extension ("master", not "master.m3u8").
			// Strip the extension so the name matches the key used in _playlists lookups.
			playlist_file_name = info::StripPlaylistFileExtension(file_name);
			is_full_request	   = false;
			return true;
		}
	}  // namespace

	struct OvtStream::PendingControlRequest
	{
		uint32_t request_id			   = 0;
		uint64_t connection_generation = 0;
		ov::String expected_application;
		std::shared_ptr<const ov::Url> target_url;
		std::optional<std::set<int32_t>> requested_track_ids;
		bool requested_full_stream				  = false;
		InventoryUpdateMode inventory_update_mode = InventoryUpdateMode::NONE;
		bool auto_handle_response				  = false;
		bool is_completed						  = false;
		ov::String response_application;
		uint32_t response_code = 0;
		ov::String response_message;
		ov::String response_payload;
	};

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

	std::shared_ptr<const ov::Url> OvtStream::CloneCurrentUrl() const
	{
		std::lock_guard<std::mutex> lock(_connection_state_lock);
		return (_curr_url != nullptr) ? _curr_url->Clone() : nullptr;
	}

	uint32_t OvtStream::AllocateRequestId()
	{
		std::lock_guard<std::mutex> lock(_connection_state_lock);
		return ++_last_request_id;
	}

	void OvtStream::ResetBufferedReceiveStateLocked()
	{
		_depacketizer = OvtDepacketizer();
		ClearPendingControlRequests();
	}

	void OvtStream::BeginConnectionGeneration()
	{
		std::lock_guard<std::mutex> handoff_lock(_connection_handoff_lock);
		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			ResetRuntimeDescribeRefreshStateLocked();
			_runtime_subscribe_attempted_revision	= 0;
			_runtime_subscribe_in_flight_request_id = 0;
			_runtime_subscribe_request_time_msec	= 0;
			ResetSharedRuntimeCapabilityStateLocked();
			ResetCurrentUpstreamSubscriptionStateLocked();
		}

		std::lock_guard<std::mutex> lock(_connection_state_lock);
		++_connection_generation;
		_current_generation_first_request_id = _last_request_id + 1;
		ResetBufferedReceiveStateLocked();
	}

	bool OvtStream::IsCurrentConnectionGeneration(uint64_t connection_generation) const
	{
		std::lock_guard<std::mutex> lock(_connection_state_lock);
		return connection_generation == _connection_generation;
	}

	std::shared_ptr<const ov::Url> OvtStream::BuildFullStreamTargetUrlFromBaseUrl(const std::shared_ptr<const ov::Url> &base_url) const
	{
		if (base_url == nullptr)
		{
			return nullptr;
		}

		auto target_url = base_url->Clone();
		if (target_url == nullptr)
		{
			return nullptr;
		}

		auto target_app_name = base_url->App();
		if (target_app_name.IsEmpty())
		{
			target_app_name = GetApplicationInfo().GetVHostAppName().GetAppName();
		}

		auto target_stream_name = base_url->Stream();
		if (target_stream_name.IsEmpty())
		{
			target_stream_name = GetName();
		}

		if (target_app_name.IsEmpty() || target_stream_name.IsEmpty())
		{
			return nullptr;
		}

		auto target_path = ov::String::FormatString("/%s/%s", target_app_name.CStr(), target_stream_name.CStr());
		if (target_url->SetPath(target_path) == false)
		{
			return nullptr;
		}

		return target_url;
	}

	std::shared_ptr<const ov::Url> OvtStream::BuildPlaylistScopedTargetUrlFromBaseUrl(const std::shared_ptr<const ov::Url> &base_url,
																					  const ov::String &playlist_file_name) const
	{
		if (playlist_file_name.IsEmpty())
		{
			return nullptr;
		}

		auto full_stream_target_url = BuildFullStreamTargetUrlFromBaseUrl(base_url);
		if (full_stream_target_url == nullptr)
		{
			return nullptr;
		}

		auto target_url = full_stream_target_url->Clone();
		if (target_url == nullptr)
		{
			return nullptr;
		}

		auto target_path = ov::String::FormatString("%s/%s", target_url->Path().CStr(), playlist_file_name.CStr());
		if (target_url->SetPath(target_path) == false)
		{
			return nullptr;
		}

		return target_url;
	}

	std::shared_ptr<const ov::Url> OvtStream::BuildRuntimeFullDescribeTargetUrl() const
	{
		return BuildFullStreamTargetUrlFromBaseUrl(CloneCurrentUrl());
	}

	bool OvtStream::IsStaleControlResponseFromPreviousGeneration(uint32_t request_id) const
	{
		std::lock_guard<std::mutex> lock(_connection_state_lock);
		return IsStaleControlResponseFromPreviousGenerationLocked(request_id);
	}

	bool OvtStream::IsStaleControlResponseFromPreviousGenerationLocked(uint32_t request_id) const
	{
		return (request_id != 0) && (request_id < _current_generation_first_request_id);
	}

	void OvtStream::Release()
	{
		std::shared_ptr<ov::Socket> client_socket;
		{
			std::lock_guard<std::mutex> handoff_lock(_connection_handoff_lock);
			std::lock_guard<std::mutex> lock(_connection_state_lock);
			++_connection_generation;
			_current_generation_first_request_id = _last_request_id + 1;
			client_socket						 = std::move(_client_socket);
			_curr_url							 = nullptr;
			ResetBufferedReceiveStateLocked();
		}

		if (client_socket != nullptr)
		{
			client_socket->Close();
		}

		InvalidateInventoryAuthority();

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

		BeginConnectionGeneration();

		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			if ((url != nullptr) && (url->File().IsEmpty() == false))
			{
				// Always store stripped form so consumers (TrySelectRepresentativePlaylistForReconnectLocked,
				// _playlists lookups) see the same convention as everywhere else.
				_preferred_playlist_reprobe_file_name = info::StripPlaylistFileExtension(url->File());
			}
			else if (_compatibility_full_stream_mode_requested == false)
			{
				_preferred_playlist_reprobe_file_name = "";
			}
		}

		{
			std::lock_guard<std::mutex> lock(_connection_state_lock);
			_curr_url = url;
		}

		{
			std::lock_guard<std::shared_mutex> lock(_packetizer_lock);
			if (_packetizer == nullptr)
			{
				_packetizer = std::make_shared<OvtPacketizer>(OvtPacketizerInterface::GetSharedPtr());
			}
		}

		ov::StopWatch stop_watch;

		// For statistics
		stop_watch.Start();
		if (!ConnectOrigin())
		{
			SetState(Stream::State::ERROR);
			Release();
			return false;
		}
		_origin_request_time_msec = stop_watch.Elapsed();

		stop_watch.Update();
		if (!RequestDescribe())
		{
			SetState(Stream::State::ERROR);
			Release();
			return false;
		}

		if (!RequestPlay())
		{
			SetState(Stream::State::ERROR);
			Release();
			return false;
		}

		if (RefreshFullInventoryAfterPlaylistBootstrap() == false)
		{
			SetState(Stream::State::ERROR);
			Release();
			return false;
		}
		_origin_response_time_msec = stop_watch.Elapsed();

		_stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(pvd::Stream::GetSharedPtr()));
		if (_stream_metrics != nullptr)
		{
			_stream_metrics->SetOriginConnectionTimeMSec(_origin_request_time_msec);
			_stream_metrics->SetOriginSubscribeTimeMSec(_origin_response_time_msec);
		}

		return true;
	}

	std::shared_ptr<const ov::Url> OvtStream::ResolveRestartTargetUrl(const std::shared_ptr<const ov::Url> &url) const
	{
		auto restart_url							  = url;
		bool full_stream_demand_present				  = false;
		bool compatibility_full_stream_mode_requested = false;
		std::shared_ptr<const ov::Url> playlist_reprobe_target_url;
		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			full_stream_demand_present				 = _shared_request_state.full_request_count > 0;
			compatibility_full_stream_mode_requested = _compatibility_full_stream_mode_requested;
			(void)TryBuildPlaylistReprobeTargetForReconnectLocked(restart_url, playlist_reprobe_target_url);
		}

		if (full_stream_demand_present)
		{
			restart_url = BuildFullStreamTargetUrlFromBaseUrl(restart_url);
			return restart_url;
		}

		if (playlist_reprobe_target_url != nullptr)
		{
			return playlist_reprobe_target_url;
		}

		if (compatibility_full_stream_mode_requested)
		{
			restart_url = BuildFullStreamTargetUrlFromBaseUrl(restart_url);
		}

		return restart_url;
	}

	bool OvtStream::RestartStream(const std::shared_ptr<const ov::Url> &url)
	{
		auto restart_url = ResolveRestartTargetUrl(url);

		if (restart_url == nullptr)
		{
			logte("[%s/%s(%u)] stream could not resolve reconnect target", GetApplicationTypeName(), GetName().CStr(), GetId());
			return false;
		}

		logti("[%s/%s(%u)] stream tries to reconnect to %s", GetApplicationTypeName(), GetName().CStr(), GetId(), restart_url->ToUrlString().CStr());
		InvalidateInventoryAuthority();
		if (InvokeStartStream(restart_url) == false)
		{
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			_compatibility_fallback_restart_pending = false;
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

	bool OvtStream::ConnectOrigin()
	{
		if (GetState() == State::PLAYING || GetState() == State::TERMINATED)
		{
			return false;
		}

		auto pool = GetOvtProvider()->GetClientSocketPool();

		if (pool == nullptr)
		{
			// Provider is not initialized
			return false;
		}

		std::lock_guard<std::mutex> connection_lock(_connection_state_lock);

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

		_client_socket = client_socket;

		SetState(State::CONNECTED);

		return true;
	}

	bool OvtStream::RequestDescribe()
	{
		if (GetState() != State::CONNECTED)
		{
			return false;
		}

		auto request_id		 = AllocateRequestId();
		auto pending_request = CreatePendingControlRequest("describe", request_id);
		if (pending_request == nullptr)
		{
			return false;
		}

		pending_request->target_url			   = CloneCurrentUrl();
		pending_request->inventory_update_mode = ((pending_request->target_url != nullptr) && pending_request->target_url->File().IsEmpty())
													 ? InventoryUpdateMode::REPLACE_ALL
													 : InventoryUpdateMode::UPSERT_PARTIAL;

		if (SendControlRequest("describe", request_id, pending_request->target_url) == false)
		{
			RemovePendingControlRequest(request_id);
			return false;
		}

		return ReceiveDescribe(request_id);
	}

	bool OvtStream::ReceiveDescribe(uint32_t request_id)
	{
		if (WaitForPendingControlResponse(request_id, OVT_TIMEOUT_MSEC) == false)
		{
			RemovePendingControlRequest(request_id);
			logte("%s/%s(%u) - Timed out waiting for describe response", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			return false;
		}

		{
			std::lock_guard<std::mutex> handoff_lock(_connection_handoff_lock);
			auto pending_request = TakePendingControlRequest(request_id);
			if (pending_request == nullptr)
			{
				logte("%s/%s(%u) - Could not resolve pending describe response (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), request_id);
				return false;
			}

			if (IsCurrentConnectionGeneration(pending_request->connection_generation) == false)
			{
				logtw("%s/%s(%u) - Drop stale describe response after generation boundary (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), request_id);
				return false;
			}

			if (HandleDescribeResponse(*pending_request) == false)
			{
				return false;
			}

			SetState(State::DESCRIBED);
		}

		return true;
	}

	bool OvtStream::HandleDescribeResponse(const PendingControlRequest &pending_request)
	{
		const bool replace_inventory = pending_request.inventory_update_mode == InventoryUpdateMode::REPLACE_ALL;

		if (pending_request.response_application.UpperCaseString() != pending_request.expected_application)
		{
			logte("An invalid response : wrong application : %s", pending_request.response_application.CStr());
			return false;
		}

		if (pending_request.response_code != 200)
		{
			logte("Describe : Server Failure : %d (%s)", pending_request.response_code, pending_request.response_message.CStr());
			return false;
		}

		ov::JsonObject object = ov::Json::Parse(pending_request.response_payload);

		if (object.IsNull())
		{
			logte("An invalid response : Json format");
			return false;
		}

		Json::Value &json_contents = object.GetJsonValue()["contents"];

		if (json_contents.isNull())
		{
			logte("An invalid response : There is no contents");
			return false;
		}

		// Parse stream and add track
		auto json_version = json_contents["version"];
		auto json_capabilities = json_contents["capabilities"];
		auto json_stream = json_contents["stream"];
		auto json_tracks = json_stream["tracks"];
		auto json_playlists = json_stream["playlists"];

		// Validation
		bool force_runtime_widening_unsupported = false;
		if (json_version.isUInt() == false)
		{
			logtw("Describe response is missing a valid OVT version. Runtime widening will stay disabled for compatibility.");
			force_runtime_widening_unsupported = true;
		}
		else
		{
			uint32_t ovt_sig_version = json_version.asUInt();
			if (ovt_sig_version != OVT_SIGNALING_VERSION)
			{
				logtw("OVT version mismatch: %X, expected %X. Runtime widening will stay disabled for compatibility.", ovt_sig_version, OVT_SIGNALING_VERSION);
				force_runtime_widening_unsupported = true;
			}
		}

		// renditions is optional
		if (json_stream["appName"].isNull() || json_stream["streamName"].isNull() || json_stream["tracks"].isNull() ||
			!json_tracks.isArray())
		{
			logte("Invalid json payload : stream");
			return false;
		}

		// Latest version origin server sends UUID of origin stream
		if (json_stream["originStreamUUID"].isString())
		{
			SetOriginStreamUUID(json_stream["originStreamUUID"].asString().c_str());
		}

		std::vector<std::shared_ptr<const info::Playlist>> parsed_playlists;
		parsed_playlists.reserve(json_playlists.size());

		// Renditions

		for (size_t i = 0; i < json_playlists.size(); i++)
		{
			auto json_playlist = json_playlists[static_cast<int>(i)];

			// Validate
			if (json_playlist["name"].isNull() || json_playlist["fileName"].isNull() ||
				!json_playlist["options"].isObject() || !json_playlist["renditions"].isArray())
			{
				logte("Invalid json payload : playlist");
				return false;
			}

			ov::String playlist_name = json_playlist["name"].asString().c_str();
			ov::String playlist_file_name = json_playlist["fileName"].asString().c_str();

			auto playlist = std::make_shared<info::Playlist>(playlist_name, playlist_file_name, false);

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
				
				playlist->AddRendition(rendition);
			}

			logti("%s", playlist->ToString().CStr());

			parsed_playlists.emplace_back(playlist);
		}

		//SetName(json_stream["streamName"].asString().c_str());
		std::vector<std::shared_ptr<MediaTrack>> parsed_tracks;
		parsed_tracks.reserve(json_tracks.size());

		for (size_t i = 0; i < json_tracks.size(); i++)
		{
			auto json_track = json_tracks[static_cast<int>(i)];
			std::shared_ptr<MediaTrack> new_track = std::make_shared<MediaTrack>();

			// Validation
			if (!json_track["id"].isUInt() || !json_track["name"].isString() || !json_track["codecId"].isUInt() || !json_track["mediaType"].isUInt() ||
				!json_track["timebaseNum"].isUInt() || !json_track["timebaseDen"].isUInt() ||
				!json_track["bitrate"].isUInt() ||
				!json_track["startFrameTime"].isUInt64() || !json_track["lastFrameTime"].isUInt64())
			{
				logte("Invalid json track [%zu]", i);
				return false;
			}

			new_track->SetId(json_track["id"].asUInt());
			new_track->SetVariantName(json_track["name"].asString().c_str());
			new_track->SetPublicName(json_track["publicName"].asString().c_str());
			new_track->SetLanguage(json_track["language"].asString().c_str());
			new_track->SetCharacteristics(json_track["characteristics"].asString().c_str());
			new_track->SetCodecId(static_cast<cmn::MediaCodecId>(json_track["codecId"].asUInt()));
			new_track->SetMediaType(static_cast<cmn::MediaType>(json_track["mediaType"].asUInt()));
			new_track->SetTimeBase(json_track["timebaseNum"].asUInt(), json_track["timebaseDen"].asUInt());
			new_track->SetBitrateByConfig(json_track["bitrate"].asUInt());
			new_track->SetStartFrameTime(json_track["startFrameTime"].asUInt64());
			new_track->SetLastFrameTime(json_track["lastFrameTime"].asUInt64());

			// video or audio
			if (new_track->GetMediaType() == cmn::MediaType::Video)
			{
				auto json_video_track = json_track["videoTrack"];
				if (json_video_track.isNull())
				{
					logte("Invalid json videoTrack");
					return false;
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
					logte("Invalid json audioTrack");
					return false;
				}

				new_track->SetSampleRate(json_audio_track["samplerate"].asUInt());
				if (new_track->GetSampleRate() == 0)
				{
					logte("Audio track(%u) received from origin has samplerate=0. The origin may have sent an invalid AudioSpecificConfig.", new_track->GetId());
				}
				new_track->SetSampleFormat(static_cast<cmn::AudioSample::Format>(json_audio_track["sampleFormat"].asInt()));
				new_track->SetChannelLayout(static_cast<cmn::AudioChannel::Layout>(json_audio_track["layout"].asUInt()));
			}

			auto decoder_config = json_track["decoderConfig"];
			if (decoder_config.isString())
			{
				auto config_data = ov::Base64::Decode(decoder_config.asString().c_str());
				auto decoder_config = DecoderConfigurationRecordParser::Parse(new_track->GetCodecId(), config_data);
				new_track->SetDecoderConfigurationRecord(decoder_config);
			}

			parsed_tracks.emplace_back(std::move(new_track));
		}

		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			auto runtime_widening_capability =
				ParseRuntimeWideningCapability(json_capabilities, _shared_runtime_capability_state.runtime_widening);
			if (force_runtime_widening_unsupported)
			{
				runtime_widening_capability = ovt::CapabilitySupport::UNSUPPORTED;
			}

			_shared_runtime_capability_state.runtime_widening = runtime_widening_capability;

			if (replace_inventory)
			{
				_playlists.clear();
				_tracks.clear();
				_video_tracks.clear();
				_audio_tracks.clear();
				_track_group_map.clear();
				_public_label_map.clear();
			}

			for (const auto &playlist : parsed_playlists)
			{
				if (playlist != nullptr)
				{
					// Normalize the map key so it matches the convention every consumer uses
					// (URL-derived lookups always go through StripPlaylistFileExtension before
					// indexing into _playlists). If the origin happens to send a fileName with
					// an extension (e.g. "master.m3u8") the key would otherwise miss every
					// lookup and silently push the system into compatibility fallback.
					_playlists[info::StripPlaylistFileExtension(playlist->GetFileName())] = playlist;
				}
			}

			for (const auto &track : parsed_tracks)
			{
				if (track == nullptr)
				{
					continue;
				}

				if (replace_inventory)
				{
					AddTrack(track);
				}
				else
				{
					UpdateTrack(track);
				}
			}

			UpdateInventorySnapshotStateLocked(replace_inventory);
			RecalculateActiveRequestStateLocked();
		}

		// logti("[%s/%s(%u)] stream has been described . %s", GetApplicationTypeName(), GetName().CStr(), GetId(), payload.CStr());
		return true;
	}

	void OvtStream::MaybeRequestRuntimeFullDescribeRefresh()
	{
		if (GetState() != State::PLAYING)
		{
			return;
		}

		{
			std::lock_guard<std::mutex> lock(_connection_state_lock);
			if ((_client_socket == nullptr) || (_curr_url == nullptr))
			{
				return;
			}
		}

		uint32_t request_id = 0;
		std::shared_ptr<const ov::Url> target_url;

		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			if ((_runtime_full_describe_refresh_in_flight_request_id != 0) &&
				((_runtime_full_describe_refresh_request_time_msec + OVT_TIMEOUT_MSEC) <= ov::Clock::NowMSec()))
			{
				const uint32_t timed_out_request_id = _runtime_full_describe_refresh_in_flight_request_id;
				logtw("%s/%s(%u) - Runtime full describe refresh timed out. Keep planning deferred/non-authoritative: request_id=%u",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  timed_out_request_id);
				ResetRuntimeDescribeRefreshRetryStateLocked(timed_out_request_id);
				// Drop the pending entry under ARS -- ARS -> _pending_control_requests_lock is
				// a one-way ordering, so this is deadlock-free. Doing it inside the same lock
				// scope prevents a late describe response from sneaking through
				// ProcessCompletedRuntimeControlRequests on another thread between releasing
				// ARS and clearing the entry, which would otherwise re-apply inventory/state
				// derived from a connection generation we already gave up on.
				DiscardPendingControlRequestAsTimedOut(timed_out_request_id);
			}

			if (ShouldRequestRuntimeFullDescribeRefreshLocked() == false)
			{
				return;
			}

			target_url = BuildRuntimeFullDescribeTargetUrl();
			if (target_url == nullptr)
			{
				logtw("%s/%s(%u) - Runtime full describe refresh target could not be built. Keep planning deferred/non-authoritative.", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
				return;
			}

			request_id = AllocateRequestId();
			_runtime_full_describe_refresh_attempted_revision = _active_request_state_revision;
			_runtime_full_describe_refresh_in_flight_request_id = request_id;
			_runtime_full_describe_refresh_request_time_msec = ov::Clock::NowMSec();
		}

		auto pending_request = CreatePendingControlRequest("describe", request_id);
		if (pending_request == nullptr)
		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			FinalizeRuntimeDescribeRefreshAttemptLocked(request_id, false);
			return;
		}

		pending_request->target_url = target_url;
		pending_request->inventory_update_mode = InventoryUpdateMode::REPLACE_ALL;
		pending_request->auto_handle_response = true;

		if (SendControlRequest("describe", request_id, pending_request->target_url) == false)
		{
			RemovePendingControlRequest(request_id);
			{
				std::lock_guard<std::mutex> lock(_active_request_state_lock);
				FinalizeRuntimeDescribeRefreshAttemptLocked(request_id, false);
			}

			logtw("%s/%s(%u) - Runtime full describe refresh request failed. Keep planning deferred/non-authoritative: target=%s",
				  GetApplicationInfo().GetVHostAppName().CStr(),
				  GetName().CStr(),
				  GetId(),
				  target_url->ToUrlString().CStr());
		}
	}

	bool OvtStream::RefreshFullInventoryAfterPlaylistBootstrap()
	{
		auto current_url = CloneCurrentUrl();
		if ((current_url == nullptr) || current_url->File().IsEmpty())
		{
			return true;
		}

		auto target_url = BuildFullStreamTargetUrlFromBaseUrl(current_url);
		if (target_url == nullptr)
		{
			logte("%s/%s(%u) - Could not build bootstrap full describe target from %s",
				  GetApplicationInfo().GetVHostAppName().CStr(),
				  GetName().CStr(),
				  GetId(),
				  current_url->ToUrlString().CStr());
			return false;
		}

		auto request_id = AllocateRequestId();
		auto pending_request = CreatePendingControlRequest("describe", request_id);
		if (pending_request == nullptr)
		{
			return false;
		}

		pending_request->target_url = target_url;
		pending_request->inventory_update_mode = InventoryUpdateMode::REPLACE_ALL;

		if (SendControlRequest("describe", request_id, target_url) == false)
		{
			RemovePendingControlRequest(request_id);
			logtw("%s/%s(%u) - Could not request bootstrap full describe refresh. Retry with compatibility full-stream bootstrap: target=%s",
				  GetApplicationInfo().GetVHostAppName().CStr(),
				  GetName().CStr(),
				  GetId(),
				  target_url->ToUrlString().CStr());
			return RetryBootstrapInCompatibilityMode(current_url);
		}

		if (WaitForPendingControlResponse(request_id, OVT_TIMEOUT_MSEC) == false)
		{
			RemovePendingControlRequest(request_id);
			logtw("%s/%s(%u) - Timed out waiting for bootstrap full describe refresh. Retry with compatibility full-stream bootstrap: request_id=%u target=%s",
				  GetApplicationInfo().GetVHostAppName().CStr(),
				  GetName().CStr(),
				  GetId(),
				  request_id,
				  target_url->ToUrlString().CStr());
			return RetryBootstrapInCompatibilityMode(current_url);
		}

		bool should_retry_in_compatibility_mode = false;
		{
			std::lock_guard<std::mutex> handoff_lock(_connection_handoff_lock);
			auto completed_request = TakePendingControlRequest(request_id);
			if (completed_request == nullptr)
			{
				logtw("%s/%s(%u) - Could not resolve bootstrap full describe response. Retry with compatibility full-stream bootstrap (%u)",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  request_id);
				should_retry_in_compatibility_mode = true;
			}
			else if (IsCurrentConnectionGeneration(completed_request->connection_generation) == false)
			{
				logtw("%s/%s(%u) - Drop bootstrap full describe response after generation boundary. Retry with compatibility full-stream bootstrap (%u)",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  request_id);
				should_retry_in_compatibility_mode = true;
			}
			else if (HandleDescribeResponse(*completed_request) == false)
			{
				logtw("%s/%s(%u) - Bootstrap full describe refresh response was rejected. Retry with compatibility full-stream bootstrap: request_id=%u target=%s",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  request_id,
					  target_url->ToUrlString().CStr());
				should_retry_in_compatibility_mode = true;
			}
		}

		if (should_retry_in_compatibility_mode)
		{
			return RetryBootstrapInCompatibilityMode(current_url);
		}

		return true;
	}

	bool OvtStream::RetryBootstrapInCompatibilityMode(const std::shared_ptr<const ov::Url> &playlist_scoped_url)
	{
		std::shared_ptr<const ov::Url> full_stream_target_url;
		if (TryPrepareCompatibilityBootstrapRetry(playlist_scoped_url, full_stream_target_url) == false)
		{
			return false;
		}

		logti("%s/%s(%u) - Retry bootstrap in compatibility full-stream mode: from=%s to=%s",
			  GetApplicationInfo().GetVHostAppName().CStr(),
			  GetName().CStr(),
			  GetId(),
			  playlist_scoped_url->ToUrlString().CStr(),
			  full_stream_target_url->ToUrlString().CStr());

		FinalizeCompatibilityBootstrapRetryPreparation();

		return InvokeStartStream(full_stream_target_url);
	}

	bool OvtStream::TryPrepareCompatibilityBootstrapRetry(const std::shared_ptr<const ov::Url> &playlist_scoped_url,
										 std::shared_ptr<const ov::Url> &full_stream_target_url)
	{
		full_stream_target_url.reset();

		if ((playlist_scoped_url == nullptr) || playlist_scoped_url->File().IsEmpty())
		{
			return false;
		}

		full_stream_target_url = BuildFullStreamTargetUrlFromBaseUrl(playlist_scoped_url);
		if (full_stream_target_url == nullptr)
		{
			logte("%s/%s(%u) - Could not build compatibility full-stream bootstrap target from %s",
				  GetApplicationInfo().GetVHostAppName().CStr(),
				  GetName().CStr(),
				  GetId(),
				  playlist_scoped_url->ToUrlString().CStr());
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			_shared_runtime_capability_state.runtime_widening = ovt::CapabilitySupport::UNSUPPORTED;
			_compatibility_full_stream_mode_requested = true;
			_compatibility_fallback_restart_pending = false;
			_preferred_playlist_reprobe_file_name = info::StripPlaylistFileExtension(playlist_scoped_url->File());
		}

		return true;
	}

	void OvtStream::FinalizeCompatibilityBootstrapRetryPreparation()
	{
		Release();
		SetState(State::IDLE);

		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		_shared_runtime_capability_state.runtime_widening = ovt::CapabilitySupport::UNSUPPORTED;
		_compatibility_full_stream_mode_requested = true;
		_compatibility_fallback_restart_pending = false;
	}

	void OvtStream::SetStartStreamInvokerForTest(StartStreamInvoker invoker)
	{
		_start_stream_invoker_for_test = std::move(invoker);
	}

	bool OvtStream::InvokeStartStream(const std::shared_ptr<const ov::Url> &url)
	{
		if (_start_stream_invoker_for_test)
		{
			return _start_stream_invoker_for_test(url);
		}

		return StartStream(url);
	}

	bool OvtStream::BuildRuntimeSubscribeTarget(std::shared_ptr<const ov::Url> &target_url, bool &full_stream, std::set<int32_t> &track_ids) const
	{
		target_url = BuildRuntimeFullDescribeTargetUrl();
		full_stream = false;
		track_ids.clear();

		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		if ((_shared_request_state.target_track_ids_are_authoritative == false) || (_shared_request_state.runtime_widening_required == false))
		{
			return false;
		}

		full_stream = _shared_request_state.target_requires_full_stream;
		track_ids = _shared_request_state.resolved_target_track_ids;
		return target_url != nullptr;
	}

	void OvtStream::MaybeRequestRuntimeSubscribe()
	{
		if (GetState() != State::PLAYING)
		{
			return;
		}

		{
			std::lock_guard<std::mutex> lock(_connection_state_lock);
			if ((_client_socket == nullptr) || (_curr_url == nullptr))
			{
				return;
			}
		}

		std::shared_ptr<const ov::Url> target_url;
		bool full_stream = false;
		std::set<int32_t> track_ids;
		if (BuildRuntimeSubscribeTarget(target_url, full_stream, track_ids) == false)
		{
			return;
		}

		uint32_t request_id = 0;
		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			if (_shared_runtime_capability_state.runtime_widening != ovt::CapabilitySupport::SUPPORTED)
			{
				return;
			}

			if (_runtime_subscribe_in_flight_request_id != 0)
			{
				if ((_runtime_subscribe_request_time_msec + OVT_TIMEOUT_MSEC) > ov::Clock::NowMSec())
				{
					return;
				}

				const uint32_t timed_out_request_id = _runtime_subscribe_in_flight_request_id;
				_runtime_subscribe_in_flight_request_id = 0;
				_runtime_subscribe_request_time_msec = 0;
				// A subscribe timeout proves the request was lost (network glitch, slow
				// origin, etc.) but NOT that the origin lacks runtime-widening capability.
				// Permanently downgrading to UNSUPPORTED on a transient timeout would force
				// compatibility-mode fallback for the rest of the connection. Instead, drop
				// the in-flight tracking and reset the attempted-revision marker so the next
				// revision change can retry. Capability stays at its current level
				// (typically SUPPORTED, since we wouldn't have sent a subscribe otherwise).
				_runtime_subscribe_attempted_revision = 0;
				RecalculateActiveRequestStateLocked();
				// Drop the pending entry and stamp it as recently-completed so a late 200
				// cannot retroactively apply stale subscribe state. ARS ->
				// _pending_control_requests_lock is a one-way ordering, so calling Discard
				// while holding ARS is safe and avoids a window where state could be racily
				// re-mutated between releasing ARS and removing the pending entry.
				DiscardPendingControlRequestAsTimedOut(timed_out_request_id);
				return;
			}

			if (_runtime_subscribe_attempted_revision == _active_request_state_revision)
			{
				return;
			}

			request_id = AllocateRequestId();
			_runtime_subscribe_in_flight_request_id = request_id;
			_runtime_subscribe_request_time_msec = ov::Clock::NowMSec();
			_runtime_subscribe_attempted_revision = _active_request_state_revision;
		}

		auto pending_request = CreatePendingControlRequest("subscribe", request_id);
		if (pending_request == nullptr)
		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			if (_runtime_subscribe_in_flight_request_id == request_id)
			{
				_runtime_subscribe_in_flight_request_id = 0;
				_runtime_subscribe_request_time_msec = 0;
			}
			return;
		}

		pending_request->target_url = target_url;
		pending_request->auto_handle_response = true;
		pending_request->requested_full_stream = full_stream;
		if (full_stream == false)
		{
			pending_request->requested_track_ids = track_ids;
		}

		Json::Value contents;
		contents["fullStream"] = full_stream;
		if (full_stream == false)
		{
			for (auto track_id : track_ids)
			{
				contents["trackIds"].append(track_id);
			}
		}

		if (SendControlRequest("subscribe", request_id, target_url, contents) == false)
		{
			RemovePendingControlRequest(request_id);
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			if (_runtime_subscribe_in_flight_request_id == request_id)
			{
				_runtime_subscribe_in_flight_request_id = 0;
				_runtime_subscribe_request_time_msec = 0;
				_runtime_subscribe_attempted_revision = 0;
			}
		}
	}

	bool OvtStream::RequestPlay()
	{
		if (GetState() != State::DESCRIBED)
		{
			logte("%s/%s(%u) - Could not request to play. Before receiving describe.", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			return false;
		}

		auto request_id = AllocateRequestId();
		auto pending_request = CreatePendingControlRequest("play", request_id);
		if (pending_request == nullptr)
		{
			return false;
		}

		pending_request->target_url = CloneCurrentUrl();

		if (SendControlRequest("play", request_id, pending_request->target_url) == false)
		{
			RemovePendingControlRequest(request_id);
			logte("%s/%s(%u) - Could not request to play. Socket send error", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			return false;
		}

		return ReceivePlay(request_id);
	}

	bool OvtStream::ReceivePlay(uint32_t request_id)
	{
		if (WaitForPendingControlResponse(request_id, OVT_TIMEOUT_MSEC) == false)
		{
			RemovePendingControlRequest(request_id);
			logte("%s/%s(%u) - Could not receive message", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
			return false;
		}

		{
			std::lock_guard<std::mutex> handoff_lock(_connection_handoff_lock);
			auto pending_request = TakePendingControlRequest(request_id);
			if (pending_request == nullptr)
			{
				logte("%s/%s(%u) - Could not resolve pending play response (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), request_id);
				return false;
			}

			if (IsCurrentConnectionGeneration(pending_request->connection_generation) == false)
			{
				logtw("%s/%s(%u) - Drop stale play response after generation boundary (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), request_id);
				return false;
			}

			if (HandlePlayResponse(*pending_request) == false)
			{
				return false;
			}

			SetState(State::PLAYING);
		}

		return true;
	}

	bool OvtStream::HandlePlayResponse(const PendingControlRequest &pending_request)
	{
		if (pending_request.response_application.UpperCaseString() != pending_request.expected_application)
		{
			logte("An invalid response : application is wrong (%s).", pending_request.response_application.CStr());
			return false;
		}

		if (pending_request.response_code != 200)
		{
			logte("Play : Server Failure : %d (%s)", pending_request.response_code, pending_request.response_message.CStr());
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			ApplyCurrentUpstreamSubscriptionStateFromPlayLocked(pending_request.target_url);
			RecalculateActiveRequestStateLocked();
		}

		return true;
	}

	bool OvtStream::SendControlRequest(const char *application, uint32_t request_id, const std::shared_ptr<const ov::Url> &target_url)
	{
		Json::Value contents;
		return SendControlRequest(application, request_id, target_url, contents);
	}

	bool OvtStream::SendControlRequest(const char *application, uint32_t request_id, const std::shared_ptr<const ov::Url> &target_url, const Json::Value &contents)
	{
		if (target_url == nullptr)
		{
			return false;
		}

		Json::Value root;
		root["id"] = request_id;
		root["application"] = application;
		root["target"] = target_url->Source().CStr();
		if (contents.isNull() == false)
		{
			root["contents"] = contents;
		}

		auto message = ov::Json::Stringify(root).ToData(false);

		std::shared_lock<std::shared_mutex> lock(_packetizer_lock);
		if ((_packetizer == nullptr) || (_packetizer->PacketizeMessage(OVT_PAYLOAD_TYPE_MESSAGE_REQUEST, ov::Clock::NowMSec(), message) == false))
		{
			return false;
		}

		return true;
	}

	OvtStream::PendingControlRequestPtr OvtStream::CreatePendingControlRequest(const ov::String &application, uint32_t request_id)
	{
		auto pending_request = std::make_shared<PendingControlRequest>();
		pending_request->request_id = request_id;
		pending_request->expected_application = application.UpperCaseString();

		std::lock_guard<std::mutex> connection_lock(_connection_state_lock);
		pending_request->connection_generation = _connection_generation;

		std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
		auto [it, inserted] = _pending_control_requests.emplace(request_id, pending_request);
		if (inserted == false)
		{
			logte("%s/%s(%u) - Pending control request already exists (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), request_id);
			return nullptr;
		}

		return it->second;
	}

	OvtStream::PendingControlRequestPtr OvtStream::TakePendingControlRequest(uint32_t request_id)
	{
		std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
		auto it = _pending_control_requests.find(request_id);
		if (it == _pending_control_requests.end())
		{
			return nullptr;
		}

		auto pending_request = it->second;
		_pending_control_requests.erase(it);
		if ((pending_request != nullptr) && pending_request->is_completed)
		{
			RememberCompletedControlRequestIdLocked(request_id);
		}
		return pending_request;
	}

	void OvtStream::RemovePendingControlRequest(uint32_t request_id)
	{
		std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
		_pending_control_requests.erase(request_id);
	}

	void OvtStream::DiscardPendingControlRequestAsTimedOut(uint32_t request_id)
	{
		if (request_id == 0)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
		_pending_control_requests.erase(request_id);
		// Treat the timed-out id as "recently completed" so a late response is silently
		// dropped via the duplicate-detection branch instead of being logged as an
		// unexpected id (which would otherwise tear down the dispatch loop).
		RememberCompletedControlRequestIdLocked(request_id);
	}

	void OvtStream::ClearPendingControlRequests()
	{
		std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
		_pending_control_requests.clear();
		_recently_completed_control_request_ids.clear();
	}

	bool OvtStream::IsPendingControlRequestCompleted(uint32_t request_id)
	{
		std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
		auto it = _pending_control_requests.find(request_id);
		return (it != _pending_control_requests.end()) && (it->second != nullptr) && it->second->is_completed;
	}

	void OvtStream::RememberCompletedControlRequestIdLocked(uint32_t request_id)
	{
		if (request_id == 0)
		{
			return;
		}

		constexpr size_t kMaxRecentlyCompletedControlRequestIds = 64;
		_recently_completed_control_request_ids.emplace_back(request_id);
		while (_recently_completed_control_request_ids.size() > kMaxRecentlyCompletedControlRequestIds)
		{
			_recently_completed_control_request_ids.pop_front();
		}
	}

	bool OvtStream::WasRecentlyCompletedControlRequestId(uint32_t request_id)
	{
		std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
		return std::find(_recently_completed_control_request_ids.begin(),
						 _recently_completed_control_request_ids.end(),
						 request_id) != _recently_completed_control_request_ids.end();
	}

	bool OvtStream::CompletePendingControlResponse(uint32_t request_id,
												   const ov::String &application,
												   uint32_t code,
												   const ov::String &message,
												   const ov::String &payload)
	{
		std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
		auto it = _pending_control_requests.find(request_id);
		if ((it == _pending_control_requests.end()) || (it->second == nullptr))
		{
			return false;
		}

		if (it->second->is_completed)
		{
			RememberCompletedControlRequestIdLocked(request_id);
			logte("%s/%s(%u) - Duplicate control response received (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), request_id);
			return false;
		}

		it->second->response_application = application;
		it->second->response_code = code;
		it->second->response_message = message;
		it->second->response_payload = payload;
		it->second->is_completed = true;
		return true;
	}

	void OvtStream::ProcessCompletedRuntimeControlRequests()
	{
		std::vector<PendingControlRequestPtr> completed_requests;

		{
			std::lock_guard<std::mutex> lock(_pending_control_requests_lock);
			for (auto it = _pending_control_requests.begin(); it != _pending_control_requests.end();)
			{
				auto pending_request = it->second;
				if ((pending_request == nullptr) || (pending_request->auto_handle_response == false) || (pending_request->is_completed == false))
				{
					++it;
					continue;
				}

				completed_requests.emplace_back(std::move(pending_request));
				it = _pending_control_requests.erase(it);
			}
		}

		for (const auto &pending_request : completed_requests)
		{
			if (pending_request == nullptr)
			{
				continue;
			}

			bool drop_stale_response = false;
			bool describe_apply_failed = false;
			bool subscribe_apply_failed = false;
			bool unexpected_auto_handled_response = false;

			{
				std::lock_guard<std::mutex> handoff_lock(_connection_handoff_lock);
				if (IsCurrentConnectionGeneration(pending_request->connection_generation) == false)
				{
					drop_stale_response = true;
				}
				else if (pending_request->expected_application == "DESCRIBE")
				{
					describe_apply_failed = (HandleDescribeResponse(*pending_request) == false);
				}
				else if (pending_request->expected_application == "SUBSCRIBE")
				{
					subscribe_apply_failed = (HandleSubscribeResponse(*pending_request) == false);
				}
				else
				{
					unexpected_auto_handled_response = true;
				}
			}

			if (drop_stale_response)
			{
				logtw("%s/%s(%u) - Drop stale auto-handled control response after generation boundary (%s:%u)",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  pending_request->expected_application.CStr(),
					  pending_request->request_id);
			}
			else if (describe_apply_failed)
			{
				logtw("%s/%s(%u) - Runtime full describe refresh response failed. Keep planning deferred/non-authoritative: request_id=%u target=%s",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  pending_request->request_id,
					  (pending_request->target_url != nullptr) ? pending_request->target_url->ToUrlString().CStr() : "(null)");
			}
			else if (subscribe_apply_failed)
			{
				logtw("%s/%s(%u) - Runtime subscribe response failed. Compatibility fallback may follow: request_id=%u target=%s",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  pending_request->request_id,
					  (pending_request->target_url != nullptr) ? pending_request->target_url->ToUrlString().CStr() : "(null)");
			}
			else if (unexpected_auto_handled_response)
			{
				logtw("%s/%s(%u) - Unexpected auto-handled control response (%s:%u)",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  pending_request->expected_application.CStr(),
					  pending_request->request_id);
			}

			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			FinalizeRuntimeDescribeRefreshAttemptLocked(pending_request->request_id, describe_apply_failed == false);

			if ((pending_request->request_id == 0) || (_runtime_subscribe_in_flight_request_id == pending_request->request_id))
			{
				_runtime_subscribe_in_flight_request_id = 0;
				_runtime_subscribe_request_time_msec = 0;
			}
		}
	}

	bool OvtStream::HandleSubscribeResponse(const PendingControlRequest &pending_request)
	{
		if (pending_request.response_application.UpperCaseString() != pending_request.expected_application)
		{
			logte("Subscribe : Invalid response application (%s)", pending_request.response_application.CStr());
			return false;
		}

		if (pending_request.response_code != 200)
		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			// Only 400 / 404 indicate an actual capability problem (subscribe selection
			// rejected per requirements.md section 4.4 / spec section 4.4). Treating 5xx the
			// same way would permanently disable runtime widening on the connection because
			// of a transient origin failure (memory pressure, restart, etc.). Leave capability
			// untouched on 5xx so the next revision change retries; only downgrade for
			// documented rejection codes.
			if ((pending_request.response_code == 400) || (pending_request.response_code == 404))
			{
				_shared_runtime_capability_state.runtime_widening = ovt::CapabilitySupport::UNSUPPORTED;
			}
			else
			{
				// Transient failure (5xx, etc.). Mirror the timeout path's explicit reset of
				// `_runtime_subscribe_attempted_revision = 0` so the retry doesn't depend on
				// RecalculateActiveRequestStateLocked's unconditional revision++ side effect
				// (requirements.md sections 4.8 / 4.10 require capability to be re-judgeable
				// without relying on incidental bookkeeping).
				_runtime_subscribe_attempted_revision = 0;
			}
			RecalculateActiveRequestStateLocked();
			return false;
		}

		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		_shared_runtime_capability_state.runtime_widening = ovt::CapabilitySupport::SUPPORTED;
		ApplyCurrentUpstreamSubscriptionStateFromSubscribeLocked(pending_request.requested_full_stream, pending_request.requested_track_ids);
		RecalculateActiveRequestStateLocked();
		return true;
	}

	bool OvtStream::HandleDescribeResponseForTest(const ov::String &expected_application,
								   const ov::String &response_application,
								   uint32_t response_code,
								   const ov::String &response_payload,
								   InventoryUpdateMode inventory_update_mode)
	{
		PendingControlRequest pending_request;
		pending_request.expected_application = expected_application;
		pending_request.response_application = response_application;
		pending_request.response_code = response_code;
		pending_request.response_payload = response_payload;
		pending_request.inventory_update_mode = inventory_update_mode;
		return HandleDescribeResponse(pending_request);
	}

	bool OvtStream::QueueCompletedRuntimeDescribeRefreshForTest(uint32_t request_id,
												  const std::shared_ptr<const ov::Url> &target_url,
												  uint32_t response_code,
												  const ov::String &response_message,
												  const ov::String &response_payload)
	{
		auto pending_request = CreatePendingControlRequest("describe", request_id);
		if (pending_request == nullptr)
		{
			return false;
		}

		pending_request->target_url = target_url;
		pending_request->auto_handle_response = true;
		pending_request->is_completed = true;
		pending_request->response_application = "describe";
		pending_request->response_code = response_code;
		pending_request->response_message = response_message;
		pending_request->response_payload = response_payload;
		return true;
	}

	bool OvtStream::HandleSubscribeResponseForTest(const ov::String &expected_application,
								   const ov::String &response_application,
								   uint32_t response_code,
								   bool requested_full_stream,
										 const std::optional<std::set<int32_t>> &requested_track_ids)
	{
		PendingControlRequest pending_request;
		pending_request.expected_application = expected_application;
		pending_request.response_application = response_application;
		pending_request.response_code = response_code;
		pending_request.requested_full_stream = requested_full_stream;
		pending_request.requested_track_ids = requested_track_ids;
		return HandleSubscribeResponse(pending_request);
	}

	bool OvtStream::WaitForPendingControlResponse(uint32_t request_id, uint32_t timeout_msec)
	{
		ov::StopWatch stop_watch;
		stop_watch.Start();

		while (stop_watch.IsElapsed(timeout_msec) == false)
		{
			if (IsPendingControlRequestCompleted(request_id))
			{
				return true;
			}

			while (true)
			{
				bool handled = false;
				bool stop_requested = false;
				if (DispatchBufferedControlMessage(&handled, &stop_requested) == false)
				{
					return false;
				}

				if (handled == false)
				{
					break;
				}

				if (stop_requested)
				{
					logte("%s/%s(%u) - Unexpected stop while waiting for control response (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), request_id);
					return false;
				}

				if (IsPendingControlRequestCompleted(request_id))
				{
					return true;
				}
			}

			auto receive_result = ReceivePacket(false);
			switch (receive_result)
			{
				case ReceivePacketResult::COMPLETE:
				case ReceivePacketResult::INCOMPLETE:
				case ReceivePacketResult::TIMEOUT:
					break;

				default:
					logte("%s/%s(%u) - Could not receive control response : err(%d)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), static_cast<uint8_t>(receive_result));
					return false;
			}
		}

		return false;
	}

	bool OvtStream::DispatchBufferedControlMessage(bool *handled, bool *stop_requested, bool *has_buffered_packets)
	{
		if (handled != nullptr)
		{
			*handled = false;
		}

		if (stop_requested != nullptr)
		{
			*stop_requested = false;
		}

		if (has_buffered_packets != nullptr)
		{
			*has_buffered_packets = false;
		}

		std::lock_guard<std::mutex> lock(_connection_state_lock);
		if (_depacketizer.IsAvailableMessage() == false)
		{
			return true;
		}

		auto message = _depacketizer.PopMessage();
		if (has_buffered_packets != nullptr)
		{
			*has_buffered_packets = _depacketizer.IsAvailableMediaPacket() || _depacketizer.IsAvailableMessage();
		}

		if (handled != nullptr)
		{
			*handled = true;
		}

		return DispatchControlMessageLocked(message, stop_requested);
	}

	bool OvtStream::DispatchControlMessage(const std::shared_ptr<ov::Data> &message, bool *stop_requested)
	{
		std::lock_guard<std::mutex> lock(_connection_state_lock);
		return DispatchControlMessageLocked(message, stop_requested);
	}

	bool OvtStream::DispatchControlMessageLocked(const std::shared_ptr<ov::Data> &message, bool *stop_requested)
	{
		if (stop_requested != nullptr)
		{
			*stop_requested = false;
		}

		if ((message == nullptr) || (message->GetLength() <= 0))
		{
			logte("An invalid response : payload is empty");
			return false;
		}

		ov::String payload(message->GetDataAs<char>(), message->GetLength());
		ov::JsonObject object = ov::Json::Parse(payload);
		if (object.IsNull())
		{
			logte("An invalid response : Json format");
			return false;
		}

		Json::Value &json_application = object.GetJsonValue()["application"];
		if (json_application.isNull() || !json_application.isString())
		{
			logte("An invalid response : There are no required keys");
			return false;
		}

		ov::String application = json_application.asString().c_str();
		if (application.UpperCaseString() == "STOP")
		{
			if (stop_requested != nullptr)
			{
				*stop_requested = true;
			}

			return true;
		}

		Json::Value &json_id = object.GetJsonValue()["id"];
		Json::Value &json_code = object.GetJsonValue()["code"];
		Json::Value &json_message = object.GetJsonValue()["message"];
		if (!json_id.isUInt() || !json_code.isUInt() || json_message.isNull())
		{
			logte("An invalid response : There are no required keys");
			return false;
		}

		auto response_request_id = json_id.asUInt();
		if (CompletePendingControlResponse(response_request_id, application, json_code.asUInt(), json_message.asString().c_str(), payload) == false)
		{
			if (IsStaleControlResponseFromPreviousGenerationLocked(response_request_id))
			{
				logtw("%s/%s(%u) - Ignore stale control response from previous generation (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), response_request_id);
				return true;
			}

			if (WasRecentlyCompletedControlRequestId(response_request_id))
			{
				logtw("%s/%s(%u) - Ignore duplicate control response for already completed request (%u)",
					  GetApplicationInfo().GetVHostAppName().CStr(),
					  GetName().CStr(),
					  GetId(),
					  response_request_id);
				return true;
			}

			logte("%s/%s(%u) - Unexpected control response id (%u)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), response_request_id);
			return false;
		}

		return true;
	}

	bool OvtStream::RequestStop()
	{
		if (GetState() != State::PLAYING)
		{
			return false;
		}

		auto request_id = AllocateRequestId();
		auto target_url = CloneCurrentUrl();
		if (SendControlRequest("stop", request_id, target_url) == false)
		{
			return false;
		}

		return true;
	}

	bool OvtStream::OnOvtPacketized(std::shared_ptr<OvtPacket> &packet)
	{
		std::lock_guard<std::mutex> lock(_connection_state_lock);
		auto client_socket = _client_socket;
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

	OvtStream::ReceivePacketResult OvtStream::ReceivePacket(bool non_block)
	{
		uint8_t buffer[65535];
		size_t read_bytes = 0ULL;

		std::lock_guard<std::mutex> lock(_connection_state_lock);

		auto client_socket = _client_socket;
		if (client_socket == nullptr)
		{
			logte("[%s/%s] Could not receive packet : socket is null", GetApplicationName(), GetName().CStr());
			return ReceivePacketResult::ERROR;
		}

		auto error = client_socket->Recv(buffer, 65535, &read_bytes, non_block);
		if (read_bytes == 0)
		{
			if (error != nullptr)
			{
				logte("[%s/%s] An error occurred while receiving packet: %s", GetApplicationName(), GetName().CStr(), error->What());
				client_socket->Close();
				return ReceivePacketResult::DISCONNECTED;
			}
			else
			{
				if (non_block)
				{
					// retry later
					return ReceivePacketResult::INCOMPLETE;
				}
				else
				{
					// timeout
					return ReceivePacketResult::TIMEOUT;
				}
			}
		}

		if (_depacketizer.AppendPacket(buffer, read_bytes) == false)
		{
			logte("[%s/%s] An error occurred while parsing packet: Invalid packet", GetApplicationName(), GetName().CStr());
			return ReceivePacketResult::ERROR;
		}

		return ReceivePacketResult::COMPLETE;
	}

	int OvtStream::GetFileDescriptorForDetectingEvent()
	{
		std::lock_guard<std::mutex> lock(_connection_state_lock);
		auto client_socket = _client_socket;
		if (client_socket == nullptr)
		{
			return -1;
		}
		return client_socket->GetNativeHandle();
	}

	bool OvtStream::HasAuthoritativeInventorySnapshotLocked() const
	{
		return _inventory_snapshot_state == InventorySnapshotState::FULL_SNAPSHOT;
	}

	void OvtStream::UpdateInventorySnapshotStateLocked(bool inventory_replace_applied)
	{
		if (inventory_replace_applied)
		{
			_inventory_snapshot_state = InventorySnapshotState::FULL_SNAPSHOT;
			return;
		}

		if (_inventory_snapshot_state != InventorySnapshotState::FULL_SNAPSHOT)
		{
			_inventory_snapshot_state = InventorySnapshotState::PARTIAL;
		}
	}

	void OvtStream::RegisterDownstreamSession(uint32_t session_id,
									 const std::shared_ptr<const ov::Url> &requested_url,
									 const std::shared_ptr<const ov::Url> &final_url,
									 const std::optional<std::set<int32_t>> &authoritative_resolved_track_ids)
	{
		if ((requested_url == nullptr) && (final_url == nullptr))
		{
			return;
		}

		ActiveRequestScope active_request_session;
		active_request_session.requested_url = (requested_url != nullptr) ? requested_url->Clone() : nullptr;
		active_request_session.final_url = (final_url != nullptr) ? final_url->Clone() : nullptr;
		// Treat an empty explicit set as "no hint", not "demand zero tracks".
		// An empty set as Path A input would record a session with no resolved
		// tracks: counted as demand (entry in `_active_request_sessions`) but
		// contributing nothing to `track_ref_counts`. Fall back to URL inference,
		// which resolves cleanly to full-stream or a known playlist.
		if (authoritative_resolved_track_ids.has_value() && authoritative_resolved_track_ids->empty())
		{
			active_request_session.authoritative_resolved_track_ids = std::nullopt;
		}
		else
		{
			active_request_session.authoritative_resolved_track_ids = authoritative_resolved_track_ids;
		}

		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		_active_request_sessions[session_id] = std::move(active_request_session);
		RecalculateActiveRequestStateLocked();
	}

	void OvtStream::UnregisterDownstreamSession(uint32_t session_id)
	{
		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		if (_active_request_sessions.erase(session_id) == 0)
		{
			return;
		}

		RecalculateActiveRequestStateLocked();
	}

	void OvtStream::RegisterDownstreamRequest(const ov::String &request_key,
									 const std::shared_ptr<const ov::Url> &requested_url,
									 const std::shared_ptr<const ov::Url> &final_url)
	{
		if (request_key.IsEmpty() || ((requested_url == nullptr) && (final_url == nullptr)))
		{
			return;
		}

		ActiveRequestScope active_request_scope;
		active_request_scope.requested_url = (requested_url != nullptr) ? requested_url->Clone() : nullptr;
		active_request_scope.final_url = (final_url != nullptr) ? final_url->Clone() : nullptr;

		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		_active_request_scopes[request_key] = std::move(active_request_scope);
		RecalculateActiveRequestStateLocked();
	}

	void OvtStream::UnregisterDownstreamRequest(const ov::String &request_key)
	{
		if (request_key.IsEmpty())
		{
			return;
		}

		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		if (_active_request_scopes.erase(request_key) == 0)
		{
			return;
		}

		RecalculateActiveRequestStateLocked();
	}

	void OvtStream::UnregisterDownstreamRequestsByKeyPrefix(const ov::String &request_key_prefix)
	{
		if (request_key_prefix.IsEmpty())
		{
			return;
		}

		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		bool any_removed = false;
		for (auto it = _active_request_scopes.begin(); it != _active_request_scopes.end();)
		{
			if (it->first.HasPrefix(request_key_prefix))
			{
				it = _active_request_scopes.erase(it);
				any_removed = true;
			}
			else
			{
				++it;
			}
		}

		if (any_removed)
		{
			RecalculateActiveRequestStateLocked();
		}
	}

	bool OvtStream::ResolveTrackIdsForPlaylist(const ov::String &playlist_file_name, std::set<int32_t> &track_ids) const
	{
		track_ids.clear();

		if (playlist_file_name.IsEmpty())
		{
			track_ids = CollectTrackIds(*this);
			return true;
		}

		auto playlist = GetPlaylist(playlist_file_name);
		if (playlist == nullptr)
		{
			return false;
		}

		track_ids = ResolvePlaylistTrackIds(*this, playlist);
		return true;
	}

	bool OvtStream::ResolveCurrentUpstreamBootstrapScopeLocked(UpstreamBootstrapScope &scope) const
	{
		scope = UpstreamBootstrapScope{};

		auto current_url = CloneCurrentUrl();
		if (current_url == nullptr)
		{
			return false;
		}

		const auto &vhost_app_name = GetApplicationInfo().GetVHostAppName();
		auto normalized_app_name = vhost_app_name.GetAppName();
		auto normalized_stream_name = GetName();
		auto normalized_stream_path = ov::String::FormatString("/%s/%s", normalized_app_name.CStr(), normalized_stream_name.CStr());

		if (TryNormalizeAuthoritativeScopeUrl(current_url.get(),
									 normalized_app_name,
									 normalized_stream_name,
									 normalized_stream_path,
									 scope.playlist_file_name,
									 scope.is_full_stream) == false)
		{
			return false;
		}

		scope.is_resolved = true;
		if (scope.is_full_stream)
		{
			return true;
		}

		if (HasAuthoritativeInventorySnapshotLocked() == false)
		{
			scope = UpstreamBootstrapScope{};
			return false;
		}

		if (ResolveTrackIdsForPlaylist(scope.playlist_file_name, scope.resolved_track_ids) == false)
		{
			scope = UpstreamBootstrapScope{};
			return false;
		}

		return true;
	}

	bool OvtStream::ResolveCurrentUpstreamTargetLocked(CurrentUpstreamSubscriptionState &state) const
	{
		state = _current_upstream_subscription_state;
		if (state.is_known)
		{
			return true;
		}

		UpstreamBootstrapScope bootstrap_scope;
		if (ResolveCurrentUpstreamBootstrapScopeLocked(bootstrap_scope) == false)
		{
			return false;
		}

		state.is_known = bootstrap_scope.is_resolved;
		state.is_full_stream = bootstrap_scope.is_full_stream;
		state.track_ids_are_authoritative = bootstrap_scope.is_full_stream || (bootstrap_scope.resolved_track_ids.empty() == false);
		state.resolved_track_ids = bootstrap_scope.resolved_track_ids;
		return state.is_known;
	}

	void OvtStream::ApplyCurrentUpstreamSubscriptionStateFromPlayLocked(const std::shared_ptr<const ov::Url> &target_url)
	{
		ResetCurrentUpstreamSubscriptionStateLocked();
		if (target_url == nullptr)
		{
			return;
		}

		CurrentUpstreamSubscriptionState state;
		if (target_url->File().IsEmpty())
		{
			state.is_known = true;
			state.is_full_stream = true;
			state.track_ids_are_authoritative = true;
			_current_upstream_subscription_state = std::move(state);
			return;
		}

		auto target_playlist_name = info::StripPlaylistFileExtension(target_url->File());
		std::set<int32_t> track_ids;
		// Even if we can't resolve the playlist's exact track set (local inventory
		// may lack this rendition, or origin sent a non-canonical fileName), the
		// upstream IS playlist-scoped - the play 200 proved that. Marking it
		// "unknown" would trigger an unneeded full-describe / compat fallback;
		// mark known-but-non-authoritative so widening decisions defer until a
		// later describe upgrades authority.
		state.is_known = true;
		state.is_full_stream = false;
		if (ResolveTrackIdsForPlaylist(target_playlist_name, track_ids))
		{
			state.track_ids_are_authoritative = true;
			state.resolved_track_ids = std::move(track_ids);
		}
		else
		{
			state.track_ids_are_authoritative = false;
		}
		_current_upstream_subscription_state = std::move(state);
	}

	void OvtStream::ApplyCurrentUpstreamSubscriptionStateFromSubscribeLocked(bool full_stream, const std::optional<std::set<int32_t>> &track_ids)
	{
		ResetCurrentUpstreamSubscriptionStateLocked();
		_current_upstream_subscription_state.is_known = true;
		_current_upstream_subscription_state.is_full_stream = full_stream;
		_current_upstream_subscription_state.track_ids_are_authoritative = true;
		if (track_ids.has_value())
		{
			_current_upstream_subscription_state.resolved_track_ids = *track_ids;
		}
	}

	void OvtStream::InvalidateInventoryAuthority()
	{
		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		InvalidateInventoryAuthorityLocked();
	}

	void OvtStream::InvalidateInventoryAuthorityLocked()
	{
		_inventory_snapshot_state = InventorySnapshotState::UNKNOWN;
		ResetRuntimeDescribeRefreshStateLocked();
		_runtime_subscribe_attempted_revision = 0;
		_runtime_subscribe_in_flight_request_id = 0;
		_runtime_subscribe_request_time_msec = 0;
		ResetSharedRuntimeCapabilityStateLocked();
		ResetCurrentUpstreamSubscriptionStateLocked();
		RecalculateActiveRequestStateLocked();
	}

	bool OvtStream::ShouldRequestRuntimeFullDescribeRefreshLocked(uint64_t *state_revision) const
	{
		if (state_revision != nullptr)
		{
			*state_revision = _active_request_state_revision;
		}

		const bool has_normalized_play_session_backed_demand =
			(_shared_request_state.full_request_count > 0) || (_shared_request_state.playlist_request_counts.empty() == false);
		if (has_normalized_play_session_backed_demand == false)
		{
			return false;
		}

		if (_shared_request_state.target_computation_deferred == false)
		{
			return false;
		}

		if (_shared_request_state.target_track_ids_are_authoritative)
		{
			return false;
		}

		if (_runtime_full_describe_refresh_in_flight_request_id != 0)
		{
			return false;
		}

		return _runtime_full_describe_refresh_attempted_revision != _active_request_state_revision;
	}

	void OvtStream::ResetRuntimeDescribeRefreshStateLocked()
	{
		_runtime_full_describe_refresh_attempted_revision = 0;
		_runtime_full_describe_refresh_in_flight_request_id = 0;
		_runtime_full_describe_refresh_request_time_msec = 0;
	}

	void OvtStream::ResetCurrentUpstreamSubscriptionStateLocked()
	{
		_current_upstream_subscription_state = CurrentUpstreamSubscriptionState{};
	}

	void OvtStream::ResetSharedRuntimeCapabilityStateLocked()
	{
		_shared_runtime_capability_state = SharedRuntimeCapabilityState{};
	}

	void OvtStream::ResetRuntimeDescribeRefreshRetryStateLocked(uint32_t request_id)
	{
		if ((request_id == 0) || (_runtime_full_describe_refresh_in_flight_request_id == request_id))
		{
			_runtime_full_describe_refresh_attempted_revision = 0;
			_runtime_full_describe_refresh_in_flight_request_id = 0;
			_runtime_full_describe_refresh_request_time_msec = 0;
		}
	}

	void OvtStream::ClearRuntimeDescribeRefreshInFlightLocked(uint32_t request_id)
	{
		if ((request_id == 0) || (_runtime_full_describe_refresh_in_flight_request_id == request_id))
		{
			_runtime_full_describe_refresh_in_flight_request_id = 0;
			_runtime_full_describe_refresh_request_time_msec = 0;
		}
	}

	void OvtStream::FinalizeRuntimeDescribeRefreshAttemptLocked(uint32_t request_id, bool refresh_succeeded)
	{
		if (refresh_succeeded)
		{
			ClearRuntimeDescribeRefreshInFlightLocked(request_id);
			return;
		}

		ResetRuntimeDescribeRefreshRetryStateLocked(request_id);
	}

	bool OvtStream::ShouldTriggerCompatibilityFallbackRestart(std::shared_ptr<const ov::Url> *restart_target_url)
	{
		if (restart_target_url != nullptr)
		{
			restart_target_url->reset();
		}

		{
			std::lock_guard<std::mutex> lock(_active_request_state_lock);
			if (_compatibility_fallback_restart_pending == false)
			{
				return false;
			}
		}

		if (restart_target_url != nullptr)
		{
			*restart_target_url = BuildRuntimeFullDescribeTargetUrl();
		}

		return true;
	}

	bool OvtStream::ShouldFinishProcessMediaForCompatibilityFallback(std::shared_ptr<const ov::Url> *restart_target_url)
	{
		return ShouldTriggerCompatibilityFallbackRestart(restart_target_url);
	}

	void OvtStream::RecalculateActiveRequestState()
	{
		std::lock_guard<std::mutex> lock(_active_request_state_lock);
		RecalculateActiveRequestStateLocked();
	}

	bool OvtStream::TryAccumulateActiveRequestScopeLocked(ActiveRequestScope &active_request_scope,
										  SharedRequestState &shared_request_state,
										  const ov::String &normalized_app_name,
										  const ov::String &normalized_stream_name,
										  const ov::String &normalized_stream_path) const
	{
		active_request_scope.resolved_track_ids.clear();
		active_request_scope.has_authoritative_scope = false;
		active_request_scope.playlist_file_name = "";

		bool is_full_request = false;
		if ((TryNormalizeAuthoritativeScopeUrl(active_request_scope.final_url.get(),
											   normalized_app_name,
											   normalized_stream_name,
											   normalized_stream_path,
											   active_request_scope.playlist_file_name,
											   is_full_request) == false) &&
			(TryNormalizeAuthoritativeScopeUrl(active_request_scope.requested_url.get(),
											   normalized_app_name,
											   normalized_stream_name,
											   normalized_stream_path,
											   active_request_scope.playlist_file_name,
											   is_full_request) == false))
		{
			return false;
		}

		active_request_scope.has_authoritative_scope = true;

		// Path A: explicit authoritative track set provided (e.g. an OvtPublisher
		// session that accepted a `subscribe` spanning multiple playlists). The
		// track set is the source of truth for this session's demand; URL is
		// informational and only feeds `playlist_request_counts` for reconnect
		// URL choice.
		// Do NOT bump `full_request_count` for a fileless URL - a union session
		// is not a full-stream demand.
		if (active_request_scope.authoritative_resolved_track_ids.has_value())
		{
			active_request_scope.resolved_track_ids = *active_request_scope.authoritative_resolved_track_ids;

			// Bump playlist_request_counts only when the URL names a known playlist;
			// a `full` URL with an explicit track set is an anonymous union and gets
			// neither a count entry nor a full_request_count bump.
			if (!is_full_request && !active_request_scope.playlist_file_name.IsEmpty())
			{
				shared_request_state.playlist_request_counts[active_request_scope.playlist_file_name]++;
			}

			for (auto track_id : active_request_scope.resolved_track_ids)
			{
				shared_request_state.track_ref_counts[track_id]++;
				shared_request_state.resolved_target_track_ids.emplace(track_id);
			}

			return true;
		}

		// Path B: URL-based inference (existing behaviour). URL alone can only encode
		// "full stream" or "exactly one known playlist". Anything beyond that needs the
		// authoritative track set above.
		if (is_full_request)
		{
			shared_request_state.full_request_count++;
			shared_request_state.target_requires_full_stream = true;
			return true;
		}

		shared_request_state.playlist_request_counts[active_request_scope.playlist_file_name]++;
		if (ResolveTrackIdsForPlaylist(active_request_scope.playlist_file_name, active_request_scope.resolved_track_ids) == false)
		{
			shared_request_state.unresolved_playlist_file_names.emplace(active_request_scope.playlist_file_name);
			return true;
		}

		for (auto track_id : active_request_scope.resolved_track_ids)
		{
			shared_request_state.track_ref_counts[track_id]++;
			shared_request_state.resolved_target_track_ids.emplace(track_id);
		}

		return true;
	}

	void OvtStream::RecalculateActiveRequestStateLocked()
	{
		SharedRequestState shared_request_state;
		shared_request_state.inventory_snapshot_state = _inventory_snapshot_state;
		const bool has_authoritative_inventory_snapshot = HasAuthoritativeInventorySnapshotLocked();
		const auto &vhost_app_name = GetApplicationInfo().GetVHostAppName();
		auto normalized_app_name = vhost_app_name.GetAppName();
		auto normalized_stream_name = GetName();
		auto normalized_stream_path = ov::String::FormatString("/%s/%s", normalized_app_name.CStr(), normalized_stream_name.CStr());

		for (auto &[session_id, active_request_session] : _active_request_sessions)
		{
			(void)session_id;
			TryAccumulateActiveRequestScopeLocked(active_request_session,
										shared_request_state,
										normalized_app_name,
										normalized_stream_name,
										normalized_stream_path);
		}

		for (auto &[request_key, active_request_scope] : _active_request_scopes)
		{
			(void)request_key;
			TryAccumulateActiveRequestScopeLocked(active_request_scope,
										shared_request_state,
										normalized_app_name,
										normalized_stream_name,
										normalized_stream_path);
		}

		if (shared_request_state.target_requires_full_stream)
		{
			shared_request_state.resolved_target_track_ids = CollectTrackIds(*this);
		}

		// `has_target_demand` covers every shape of active demand on this stream:
		//   - full-stream (URL has no file)            -> target_requires_full_stream
		//   - single-playlist URL                       -> playlist_request_counts non-empty
		//   - multi-playlist union (Path A above)       -> track_ref_counts non-empty
		//
		// The third clause is essential: without it, a Path-A "anonymous union"
		// session sets neither of the first two and looks like "no demand", which
		// silently aborts sticky-compat / pending-restart state in Branch 2.
		const bool has_target_demand =
			shared_request_state.target_requires_full_stream ||
			(shared_request_state.playlist_request_counts.empty() == false) ||
			(shared_request_state.track_ref_counts.empty() == false);
		shared_request_state.target_computation_deferred =
			has_target_demand && ((has_authoritative_inventory_snapshot == false) || (shared_request_state.unresolved_playlist_file_names.empty() == false));
		shared_request_state.target_track_ids_are_authoritative =
			has_authoritative_inventory_snapshot && ((has_target_demand == false) || (shared_request_state.target_computation_deferred == false));

		CurrentUpstreamSubscriptionState current_upstream_target;
		if (ResolveCurrentUpstreamTargetLocked(current_upstream_target) &&
			(current_upstream_target.is_full_stream == false) &&
			shared_request_state.target_track_ids_are_authoritative)
		{
			shared_request_state.runtime_widening_required = shared_request_state.target_requires_full_stream;
			if (shared_request_state.runtime_widening_required == false)
			{
				shared_request_state.runtime_widening_required =
					std::includes(current_upstream_target.resolved_track_ids.begin(),
								  current_upstream_target.resolved_track_ids.end(),
								  shared_request_state.resolved_target_track_ids.begin(),
								  shared_request_state.resolved_target_track_ids.end()) == false;
			}
		}

		shared_request_state.compatibility_fallback_required =
			shared_request_state.runtime_widening_required &&
			(_shared_runtime_capability_state.runtime_widening == ovt::CapabilitySupport::UNSUPPORTED);

		const bool has_playlist_only_request_demand =
			(shared_request_state.full_request_count == 0) &&
			(shared_request_state.playlist_request_counts.empty() == false);
		const bool has_remembered_playlist_reprobe_scope =
			_preferred_playlist_reprobe_file_name.IsEmpty() == false;
		const bool current_upstream_is_known_playlist_scoped =
			current_upstream_target.is_known && (current_upstream_target.is_full_stream == false);
		const bool compatibility_full_stream_is_active_or_sticky =
			_compatibility_full_stream_mode_requested &&
			((current_upstream_target.is_known == false) || current_upstream_target.is_full_stream);

		if (shared_request_state.compatibility_fallback_required)
		{
			_compatibility_full_stream_mode_requested = true;
			_compatibility_fallback_restart_pending = true;
			_compatibility_reprobe_on_next_reconnect = false;
		}
		else if (has_target_demand == false)
		{
			_compatibility_fallback_restart_pending = false;
			if (_compatibility_full_stream_mode_requested && has_remembered_playlist_reprobe_scope)
			{
				_compatibility_reprobe_on_next_reconnect = compatibility_full_stream_is_active_or_sticky;
			}
			else
			{
				_compatibility_full_stream_mode_requested = false;
				_compatibility_reprobe_on_next_reconnect = false;
			}
		}
		else if (current_upstream_is_known_playlist_scoped)
		{
			// Demand fits the playlist-scoped upstream, so drop sticky compat mode.
			// Do NOT clear `_compatibility_fallback_restart_pending` here: it is
			// Branch 1's "restart committed" signal, and a transient demand drop
			// must not cancel an in-flight commitment. Cleared only by Branch 2
			// (`has_target_demand == false`) or by `RestartStream()` once it ran.
			_compatibility_full_stream_mode_requested = false;
			_compatibility_reprobe_on_next_reconnect = false;
		}
		else if (_compatibility_full_stream_mode_requested == false)
		{
			_compatibility_fallback_restart_pending = false;
			_compatibility_reprobe_on_next_reconnect = false;
		}
		else
		{
			// Sticky compatibility mode + active demand + upstream is full-stream or unknown.
			// Clearing `_compatibility_fallback_restart_pending` here is safe: by the time
			// this branch fires the upstream is already aligned with sticky mode's goal
			// (full-stream or about to reconnect into one), so a separately committed restart
			// has effectively been satisfied.
			_compatibility_fallback_restart_pending = false;

			// Should the next reconnect reprobe the playlist URL?
			//   UNSUPPORTED                  -> yes, only path back to playlist scope
			//   UNKNOWN, no remembered scope -> yes, give playlist a chance
			//   UNKNOWN, remembered scope    -> no, respect externally-set sticky intent
			//   SUPPORTED                    -> no, origin can widen; reprobe wastes RTT
			const auto capability = _shared_runtime_capability_state.runtime_widening;
			const bool capability_proven_unable = (capability == ovt::CapabilitySupport::UNSUPPORTED);
			const bool capability_unproven_without_history =
				(capability == ovt::CapabilitySupport::UNKNOWN) &&
				(has_remembered_playlist_reprobe_scope == false);
			_compatibility_reprobe_on_next_reconnect =
				has_playlist_only_request_demand &&
				(capability_proven_unable || capability_unproven_without_history);
		}

		_shared_request_state = std::move(shared_request_state);
		_active_request_state_revision++;
	}

	bool OvtStream::TryBuildPlaylistReprobeTargetForReconnectLocked(const std::shared_ptr<const ov::Url> &base_url,
														 std::shared_ptr<const ov::Url> &target_url) const
	{
		target_url.reset();

		if (_compatibility_reprobe_on_next_reconnect == false)
		{
			return false;
		}

		ov::String representative_playlist_file_name;
		if (TrySelectRepresentativePlaylistForReconnectLocked(representative_playlist_file_name) == false)
		{
			return false;
		}

		target_url = BuildPlaylistScopedTargetUrlFromBaseUrl(base_url, representative_playlist_file_name);
		return target_url != nullptr;
	}

	bool OvtStream::TrySelectRepresentativePlaylistForReconnectLocked(ov::String &playlist_file_name) const
	{
		playlist_file_name = "";

		if (_shared_request_state.full_request_count > 0)
		{
			return false;
		}

		for (const auto &[candidate_playlist_file_name, request_count] : _shared_request_state.playlist_request_counts)
		{
			if ((request_count == 0) || candidate_playlist_file_name.IsEmpty())
			{
				continue;
			}

			playlist_file_name = candidate_playlist_file_name;
			return true;
		}

		if (_preferred_playlist_reprobe_file_name.IsEmpty())
		{
			return false;
		}

		playlist_file_name = _preferred_playlist_reprobe_file_name;
		return true;
	}

	PullStream::ProcessMediaResult OvtStream::ProcessMediaPacket()
	{
		// Non block
		auto result = ReceivePacket(true);
		if ((result != ReceivePacketResult::COMPLETE) && (result != ReceivePacketResult::INCOMPLETE))
		{
			logte("%s/%s(%u) - Could not receive packet : err(%d)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), static_cast<uint8_t>(result));
			return ProcessMediaResult::PROCESS_MEDIA_FAILURE;
		}

		ProcessCompletedRuntimeControlRequests();
		MaybeRequestRuntimeSubscribe();

		std::shared_ptr<const ov::Url> compatibility_fallback_target_url;
		if (ShouldFinishProcessMediaForCompatibilityFallback(&compatibility_fallback_target_url))
		{
			logti("%s/%s(%u) - Trigger compatibility fallback restart with full-stream upstream target: %s",
				  GetApplicationInfo().GetVHostAppName().CStr(),
				  GetName().CStr(),
				  GetId(),
				  (compatibility_fallback_target_url != nullptr) ? compatibility_fallback_target_url->ToUrlString().CStr() : "(null)");
			return PullStream::ProcessMediaResult::PROCESS_MEDIA_FINISH;
		}

		while (true)
		{
			std::shared_ptr<MediaPacket> media_packet;
			bool has_buffered_packets = false;
			uint64_t media_packet_generation = 0;

			{
				std::lock_guard<std::mutex> lock(_connection_state_lock);
				if (_depacketizer.IsAvailableMediaPacket())
				{
					media_packet = _depacketizer.PopMediaPacket();
					media_packet_generation = _connection_generation;
				}

				has_buffered_packets = _depacketizer.IsAvailableMediaPacket() || _depacketizer.IsAvailableMessage();
			}

			if (media_packet != nullptr)
			{
				media_packet->SetMsid(GetMsid());
				media_packet->SetPacketType(cmn::PacketType::OVT);

				int64_t pts = media_packet->GetPts();
				int64_t dts = media_packet->GetDts();
				int64_t duration = media_packet->GetDuration();

				AdjustTimestampByBase(media_packet->GetTrackId(), pts, dts, std::numeric_limits<int64_t>::max(), duration);
				[[maybe_unused]] auto old_pts = media_packet->GetPts();
				[[maybe_unused]] auto old_dts = media_packet->GetDts();

				media_packet->SetPts(pts);
				media_packet->SetDts(dts);
				media_packet->SetDuration(-1); // Duration should be set by MediaRouter again due to the AdjustTimestampByBase

				logtt("[%s/%s(%u)] ProcessMediaPacket : TrackId(%d) ORI_PTS(%" PRId64 ") PTS(%" PRId64 ") ORI_DTS(%" PRId64 ") DTS(%" PRId64 ") Size(%zu) MSID(%u)",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(),
					  media_packet->GetTrackId(), old_pts, media_packet->GetPts(), old_dts, media_packet->GetDts(), media_packet->GetDataLength(), GetMsid());

				bool drop_stale_media_packet = false;
				{
					std::lock_guard<std::mutex> handoff_lock(_connection_handoff_lock);
					if (IsCurrentConnectionGeneration(media_packet_generation) == false)
					{
						drop_stale_media_packet = true;
					}
					else
					{
						SendFrame(media_packet);
					}
				}

				if (drop_stale_media_packet)
				{
					logtw("%s/%s(%u) - Drop stale media packet before forwarding after generation boundary: track=%d",
						  GetApplicationInfo().GetVHostAppName().CStr(),
						  GetName().CStr(),
						  GetId(),
						  media_packet->GetTrackId());
					continue;
				}

				ProcessCompletedRuntimeControlRequests();
				MaybeRequestRuntimeFullDescribeRefresh();

				if (has_buffered_packets)
				{
					continue;
				}

				return PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS;
			}

			bool handled_message = false;
			bool stop_requested = false;
			if (DispatchBufferedControlMessage(&handled_message, &stop_requested, &has_buffered_packets) == false)
			{
				logte("An error occurred while receive data: An unexpected packet was received. Terminate stream thread : %s/%s(%u)",
					  GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
				return PullStream::ProcessMediaResult::PROCESS_MEDIA_FAILURE;
			}

			if (handled_message)
			{

				ProcessCompletedRuntimeControlRequests();

				if (stop_requested)
				{
					return PullStream::ProcessMediaResult::PROCESS_MEDIA_FINISH;
				}

				MaybeRequestRuntimeFullDescribeRefresh();

				if (has_buffered_packets)
				{
					continue;
				}

				return PullStream::ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
			}

			else
			{
				MaybeRequestRuntimeFullDescribeRefresh();
				return ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
			}
		}

		return PullStream::ProcessMediaResult::PROCESS_MEDIA_SUCCESS;
	}
}  // namespace pvd
