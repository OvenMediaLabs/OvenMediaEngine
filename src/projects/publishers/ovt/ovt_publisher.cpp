#include "ovt_publisher.h"

#include <base/info/playlist_file.h>
#include <base/ovlibrary/url.h>

#include "ovt_private.h"
#include "ovt_publisher_internal.h"
#include "ovt_session.h"

namespace ovt_pub::internal
{
	std::shared_ptr<ov::Url> BuildFullScopeUrl(const std::shared_ptr<const ov::Url> &base_url, const std::shared_ptr<OvtStream> &stream)
	{
		if ((base_url == nullptr) || (stream == nullptr))
		{
			return nullptr;
		}

		auto scoped_url = base_url->Clone();
		if (scoped_url == nullptr)
		{
			return nullptr;
		}

		auto app_name = stream->GetApplicationInfo().GetVHostAppName().GetAppName();
		auto path	  = ov::String::FormatString("/%s/%s", app_name.CStr(), stream->GetName().CStr());
		if (scoped_url->SetPath(path) == false)
		{
			return nullptr;
		}

		return scoped_url;
	}

	std::shared_ptr<ov::Url> BuildPlaylistScopeUrl(const std::shared_ptr<const ov::Url> &base_url,
												   const std::shared_ptr<OvtStream> &stream,
												   const ov::String &playlist_file_name)
	{
		auto scoped_url = BuildFullScopeUrl(base_url, stream);
		if (scoped_url == nullptr)
		{
			return nullptr;
		}

		auto app_name = stream->GetApplicationInfo().GetVHostAppName().GetAppName();
		auto path	  = ov::String::FormatString("/%s/%s/%s", app_name.CStr(), stream->GetName().CStr(), playlist_file_name.CStr());
		if (scoped_url->SetPath(path) == false)
		{
			return nullptr;
		}

		return scoped_url;
	}

	bool ParseSubscribeSelection(const Json::Value &contents, bool &full_stream, std::optional<std::set<int32_t>> &track_ids)
	{
		full_stream = false;
		track_ids.reset();

		if (contents.isNull())
		{
			full_stream = true;
			return true;
		}

		if (contents.isObject() == false)
		{
			return false;
		}

		auto json_full_stream = contents["fullStream"];
		if (json_full_stream.isBool())
		{
			full_stream = json_full_stream.asBool();
		}

		auto json_track_ids = contents["trackIds"];
		if (json_track_ids.isNull())
		{
			return full_stream;
		}

		if (json_track_ids.isArray() == false)
		{
			return false;
		}

		std::set<int32_t> parsed_track_ids;
		for (Json::ArrayIndex i = 0; i < json_track_ids.size(); ++i)
		{
			if ((json_track_ids[i].isInt() == false) && (json_track_ids[i].isUInt() == false))
			{
				return false;
			}

			parsed_track_ids.emplace(json_track_ids[i].asInt());
		}

		if (parsed_track_ids.empty())
		{
			return full_stream;
		}

		track_ids = std::move(parsed_track_ids);
		return true;
	}

	std::shared_ptr<ov::Url> ResolveCanonicalSubscribeScopeUrl(const std::shared_ptr<const ov::Url> &base_url,
															   const std::shared_ptr<OvtStream> &stream,
															   const std::optional<std::set<int32_t>> &track_ids,
															   bool full_stream)
	{
		if (full_stream || (track_ids.has_value() == false))
		{
			return BuildFullScopeUrl(base_url, stream);
		}

		std::set<int32_t> full_track_ids;
		for (const auto &[track_id, track] : stream->GetTracks())
		{
			(void)track;
			full_track_ids.emplace(track_id);
		}

		if (*track_ids == full_track_ids)
		{
			return BuildFullScopeUrl(base_url, stream);
		}

		// First, try an exact single-playlist match. This is the canonical case and gives
		// the cleanest URL representation (a real playlist file name on the wire).
		for (const auto &[file_name, playlist] : stream->GetPlaylists())
		{
			(void)playlist;

			std::set<int32_t> playlist_track_ids;
			if (stream->ResolveTrackIdsForPlaylist(file_name, playlist_track_ids) == false)
			{
				continue;
			}

			if (playlist_track_ids == *track_ids)
			{
				return BuildPlaylistScopeUrl(base_url, stream, file_name);
			}
		}

		// No single-playlist match. Try to express the request as a union of
		// playlists - covers edges that span multiple playlists with disjoint
		// tracks (e.g. video-only + audio-only, or different audio languages).
		//
		// Algorithm: take every playlist whose track set is contained in the
		// request, union them, and accept iff the union equals the request.
		// Guarantees we never widen beyond what was asked, and never accept an
		// arbitrary subset that doesn't correspond to any playlist combination.
		std::set<int32_t> covered_track_ids;
		bool any_playlist_contributed = false;
		for (const auto &[file_name, playlist] : stream->GetPlaylists())
		{
			(void)playlist;

			std::set<int32_t> playlist_track_ids;
			if (stream->ResolveTrackIdsForPlaylist(file_name, playlist_track_ids) == false)
			{
				continue;
			}

			if (playlist_track_ids.empty())
			{
				continue;
			}

			if (std::includes(track_ids->begin(), track_ids->end(),
							  playlist_track_ids.begin(), playlist_track_ids.end()))
			{
				covered_track_ids.insert(playlist_track_ids.begin(), playlist_track_ids.end());
				any_playlist_contributed = true;
			}
		}

		if (any_playlist_contributed && (covered_track_ids == *track_ids))
		{
			// The requested set is exactly the union of some subset of registered playlists.
			// No single-playlist URL fits this scope, so fall back to the stream-level
			// (full) URL for the canonical scope identifier. The actual track filter is
			// conveyed via OvtSession::_allowed_track_ids set up by the caller, so this
			// URL choice is purely decorative for monitoring/logging.
			return BuildFullScopeUrl(base_url, stream);
		}

		return nullptr;
	}

	void StoreRequestScopeOnSession(const std::shared_ptr<pub::Session> &session, const std::shared_ptr<const ov::Url> &url)
	{
		if ((session == nullptr) || (url == nullptr))
		{
			return;
		}

		session->SetRequestedUrl(url->Clone());
		session->SetFinalUrl(url->Clone());
	}
}  // namespace ovt_pub::internal

std::shared_ptr<OvtPublisher> OvtPublisher::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
{
	auto obj = std::make_shared<OvtPublisher>(server_config, router);

	if (!obj->Start())
	{
		return nullptr;
	}

	return obj;
}

OvtPublisher::OvtPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	: Publisher(server_config, router)
{
}

OvtPublisher::~OvtPublisher()
{
	logtt("OvtPublisher has been terminated finally");
}

bool OvtPublisher::Start()
{
	// Listen to localhost:<relay_port>
	auto server_config = GetServerConfig();

	const auto &ovt_config = server_config.GetBind().GetPublishers().GetOvt();

	if (ovt_config.IsParsed() == false)
	{
		logtw("%s is disabled by configuration", GetPublisherName());
		return true;
	}

	bool is_configured;
	auto &port_config = ovt_config.GetPort(&is_configured);

	if (is_configured == false)
	{
		logtw("API Server is disabled - No port is configured");
		return true;
	}

	auto &ip_list = server_config.GetIPList();
	std::vector<ov::SocketAddress> address_list;
	try
	{
		address_list = ov::SocketAddress::Create(ip_list, static_cast<uint16_t>(port_config.GetPort()));
	}
	catch (const ov::Error &e)
	{
		logte("Could not listen for %s Server: %s", GetPublisherName(), e.What());
		return false;
	}

	auto worker_count = ovt_config.GetWorkerCount(&is_configured);
	worker_count = is_configured ? worker_count : PHYSICAL_PORT_USE_DEFAULT_COUNT;

	bool result = true;
	std::vector<std::shared_ptr<PhysicalPort>> server_port_list;
	std::vector<ov::String> address_string_list;

	for (auto &address : address_list)
	{
		auto server_port = PhysicalPortManager::GetInstance()->CreatePort("OvtPub", port_config.GetSocketType(), address, worker_count);

		if (server_port == nullptr)
		{
			logte("Could not listen for %s on %s", GetPublisherName(), address.ToString().CStr());
			result = false;
			break;
		}

		server_port->AddObserver(this);
		server_port_list.push_back(server_port);

		address_string_list.emplace_back(address.ToString());
	}

	if (result)
	{
		logti("%s is listening on %s/%s...",
			  GetPublisherName(),
			  ov::String::Join(address_string_list, ", ").CStr(),
			  ov::StringFromSocketType(port_config.GetSocketType()));

		{
			std::lock_guard lock_guard{_server_port_list_mutex};
			_server_port_list = std::move(server_port_list);
		}

		return Publisher::Start();
	}

	for (auto &server_port : server_port_list)
	{
		server_port->RemoveObserver(this);
		server_port->Close();
	}

	return false;
}

bool OvtPublisher::Stop()
{
	_server_port_list_mutex.lock();
	auto server_port_list = std::move(_server_port_list);
	_server_port_list_mutex.unlock();

	auto physical_port_manager = PhysicalPortManager::GetInstance();

	for (auto &server_port : server_port_list)
	{
		server_port->RemoveObserver(this);
		physical_port_manager->DeletePort(server_port);
	}

	return Publisher::Stop();
}

bool OvtPublisher::OnCreateHost(const info::Host &host_info)
{
	return true;
}

bool OvtPublisher::OnDeleteHost(const info::Host &host_info)
{
	return true;
}

std::shared_ptr<pub::Application> OvtPublisher::OnCreatePublisherApplication(const info::Application &application_info)
{
	if (IsModuleAvailable() == false)
	{
		return nullptr;
	}

	return OvtApplication::Create(OvtPublisher::GetSharedPtrAs<pub::Publisher>(), application_info);
}

bool OvtPublisher::OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application)
{
	return true;
}

std::shared_ptr<OvtDepacketizer> OvtPublisher::GetDepacketizer(int remote_id)
{
	std::lock_guard<std::mutex> guard(_depacketizers_lock);
	std::shared_ptr<OvtDepacketizer> depacketizer;

	// if there is no depacketizer, create
	if (_depacketizers.find(remote_id) == _depacketizers.end())
	{
		depacketizer = std::make_shared<OvtDepacketizer>();
		_depacketizers[remote_id] = depacketizer;
	}
	else
	{
		return _depacketizers[remote_id];
	}

	return depacketizer;
}

bool OvtPublisher::RemoveDepacketizer(int remote_id)
{
	std::lock_guard<std::mutex> guard(_depacketizers_lock);
	_depacketizers.erase(remote_id);
	return true;
}

void OvtPublisher::OnConnected(const std::shared_ptr<ov::Socket> &remote)
{
	// NOTHING
	logti("OvtProvider is connected : %s", remote->ToString().CStr());
}

void OvtPublisher::OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
								  const ov::SocketAddress &address,
								  const std::shared_ptr<const ov::Data> &data)
{
	auto depacketizer = GetDepacketizer(remote->GetNativeHandle());

	if (depacketizer->AppendPacket(data) == false)
	{
		ResponseResult(remote, 0, "unknown", 0, 500, "Server Internals Error");
		return;
	}

	if (!depacketizer->IsAvailableMessage())
	{
		logtc("Unavailable message");
	}

	while (depacketizer->IsAvailableMessage())
	{
		auto message = depacketizer->PopMessage();

		// Parsing Payload
		ov::String payload(message->GetDataAs<char>(), message->GetLength());
		ov::JsonObject object = ov::Json::Parse(payload);

		if (object.IsNull())
		{
			ResponseResult(remote, 0, "unknown", 0, 404, "An invalid request : Json format");
			return;
		}

		Json::Value &json_request_id = object.GetJsonValue()["id"];
		Json::Value &json_request_app = object.GetJsonValue()["application"];
		Json::Value &json_request_target = object.GetJsonValue()["target"];
		Json::Value &json_request_contents = object.GetJsonValue()["contents"];

		if (json_request_id.isNull() || !json_request_id.isUInt() ||
			json_request_app.isNull() || !json_request_app.isString() ||
			json_request_target.isNull() || !json_request_target.isString())
		{
			ResponseResult(remote, 0, "unknown", 0, 404, "An invalid request : id or target or application are invalid");
			return;
		}

		uint32_t request_id = json_request_id.asUInt();
		ov::String app = json_request_app.asString().c_str();
		auto url = ov::Url::Parse(json_request_target.asString().c_str());
		if (url == nullptr)
		{
			ResponseResult(remote, 0, "unknown", json_request_id.asUInt(), 404, "An invalid request : Target is not valid");
			return;
		}

		if (app.UpperCaseString() == "DESCRIBE")
		{
			HandleDescribeRequest(remote, request_id, url);
		}
		else if (app.UpperCaseString() == "PLAY")
		{
			HandlePlayRequest(remote, request_id, url);
		}
		else if (app.UpperCaseString() == "SUBSCRIBE")
		{
			HandleSubscribeRequest(remote, request_id, url, json_request_contents);
		}
		else if (app.UpperCaseString() == "STOP")
		{
			HandleStopRequest(remote, 0, request_id, url);
		}
		else
		{
			ResponseResult(remote, 0, app.CStr(), request_id, 404, "Unknown application");
		}
	}
}

// It it only called when the OVT runs over TCP or SRT

// TODO(Getroot): If the Ovt uses UDP, OME cannot know that the connection was forcibly terminated.(Ungraceful termination)
// In this case, OME should add PING/PONG function to check if the connection is broken.
// However, this version of OVT does not need to be considered because it does not use UDP but only tcp or srt.
// If the OVT is extended to use UDP in the future, then the protocol needs to be advanced.

void OvtPublisher::OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
								  PhysicalPortDisconnectReason reason,
								  const std::shared_ptr<const ov::Error> &error)
{
	logti("OvtProvider is disconnected(%d) : %s", static_cast<uint8_t>(reason), remote->ToString().CStr());
	// disconnect means when the stream disconnects itself.
	if (reason != PhysicalPortDisconnectReason::Disconnect)
	{
		std::shared_lock<std::shared_mutex> lock(_remote_stream_map_lock);
		auto streams = _remote_stream_map.equal_range(remote->GetNativeHandle());
		for (auto it = streams.first; it != streams.second; ++it)
		{
			auto stream = it->second;
			stream->RemoveSessionByConnectorId(remote->GetNativeHandle());
		}
	}
	UnlinkRemoteFromStream(remote->GetNativeHandle());
	RemoveDepacketizer(remote->GetNativeHandle());
}

void OvtPublisher::HandleDescribeRequest(const std::shared_ptr<ov::Socket> &remote, const uint32_t request_id, const std::shared_ptr<const ov::Url> &url)
{
	auto orchestrator = ocst::Orchestrator::GetInstance();

	auto host_name = url->Host();
	auto app_name = url->App();
	auto vhost_app_name = orchestrator->ResolveApplicationNameFromDomain(host_name, app_name);
	auto stream_name = url->Stream();
	ov::String msg;
	auto playlist_file_name = (url != nullptr) ? info::StripPlaylistFileExtension(url->File()) : ov::String("");
	auto stream = std::static_pointer_cast<OvtStream>(GetStream(vhost_app_name, stream_name));
	if (stream == nullptr)
	{
		// If the stream does not exists, request to the provider
		auto error = orchestrator->RequestPullStreamWithOriginMap(url, vhost_app_name, stream_name);
		if (error != nullptr)
		{
			logte("Could not pull stream from origin map [%s/%s]: %s",
				  vhost_app_name.CStr(),
				  stream_name.CStr(),
				  error->What());

			msg.Format("There is no such stream (%s/%s)", vhost_app_name.CStr(), url->Stream().CStr());
			ResponseResult(remote, 0, "describe", request_id, 404, msg);
			return;
		}
		else
		{
			stream = std::static_pointer_cast<OvtStream>(GetStream(vhost_app_name, stream_name));
			if (stream == nullptr)
			{
				msg.Format("Could not pull the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
				ResponseResult(remote, 0, "describe", request_id, 404, msg);
				return;
			}
		}
	}

	if (stream->WaitUntilStart(3000) == false)
	{
		msg.Format("(%s/%s) stream has not started.", vhost_app_name.CStr(), url->Stream().CStr());
		ResponseResult(remote, 0, "describe", request_id, 202, msg);
		return;
	}

	Json::Value description;
	if (stream->GetDescription(description, playlist_file_name) == false)
	{
		if (playlist_file_name.IsEmpty() == false)
		{
			msg.Format("(%s/%s) stream doesn't have playlist: %s", vhost_app_name.CStr(), url->Stream().CStr(), playlist_file_name.CStr());
		}
		else
		{
			msg.Format("(%s/%s) stream doesn't have description.", vhost_app_name.CStr(), url->Stream().CStr());
		}
		ResponseResult(remote, 0, "describe", request_id, 404, msg);
		return;
	}

	ResponseResult(remote, 0, "describe", request_id, 200, "ok", description);
}

void OvtPublisher::HandlePlayRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t request_id, const std::shared_ptr<const ov::Url> &url)
{
	auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(url->Host(), url->App());

	auto app = std::static_pointer_cast<OvtApplication>(GetApplicationByName(vhost_app_name));
	if (app == nullptr)
	{
		ov::String msg;
		msg.Format("There is no such app (%s)", vhost_app_name.CStr());
		ResponseResult(remote, 0, "play", request_id, 404, msg);
		return;
	}

	auto stream = std::static_pointer_cast<OvtStream>(app->GetStream(url->Stream()));
	if (stream == nullptr)
	{
		ov::String msg;
		msg.Format("There is no such stream (%s/%s)", vhost_app_name.CStr(), url->Stream().CStr());
		ResponseResult(remote, 0, "play", request_id, 404, msg);
		return;
	}

	std::optional<std::set<int32_t>> allowed_track_ids;
	auto playlist_file_name = info::StripPlaylistFileExtension(url->File());
	if (playlist_file_name.IsEmpty() == false)
	{
		std::set<int32_t> resolved_track_ids;
		if (stream->ResolveTrackIdsForPlaylist(playlist_file_name, resolved_track_ids) == false)
		{
			ov::String msg;
			msg.Format("(%s/%s) stream doesn't have playlist: %s", vhost_app_name.CStr(), url->Stream().CStr(), playlist_file_name.CStr());
			ResponseResult(remote, 0, "play", request_id, 404, msg);
			return;
		}

		allowed_track_ids = std::move(resolved_track_ids);
	}

	// Session ID is remote socket's ID
	auto session = OvtSession::Create(app, stream, remote->GetNativeHandle(), remote, allowed_track_ids);
	if (session == nullptr)
	{
		ov::String msg;
		msg.Format("Internal Error : Cannot create session");
		ResponseResult(remote, 0, "play", request_id, 404, msg);
		return;
	}

	ovt_pub::internal::StoreRequestScopeOnSession(session, url);

	LinkRemoteWithStream(remote->GetNativeHandle(), stream);

	// Register the session BEFORE the play 200 - otherwise a subscribe arriving
	// immediately on the same TCP connection can race past AddSession and get
	// "no active play session" 404.
	if (stream->AddSession(session) == false)
	{
		// AddSession may have inserted into `_sessions` before failing (e.g. on
		// worker registration). Roll back the map entry, the remote link, and
		// the session itself - Start() already ran in OvtSession::Create, so
		// just dropping the shared_ptr would leak socket / monitoring state.
		// Send the 500 BEFORE Stop(): Stop() closes the connector socket and
		// any later write is silently dropped (client only sees TCP RST).
		stream->RemoveSession(session->GetId());
		UnlinkRemoteFromStream(remote->GetNativeHandle());
		ov::String msg;
		msg.Format("Internal Error : Cannot register session");
		ResponseResult(remote, 0, "play", request_id, 500, msg);
		session->Stop();
		return;
	}

	ResponseResult(remote, session->GetId(), "play", request_id, 200, "ok");
}

void OvtPublisher::HandleSubscribeRequest(const std::shared_ptr<ov::Socket> &remote,
										  uint32_t request_id,
										  const std::shared_ptr<const ov::Url> &url,
										  const Json::Value &contents)
{
	auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(url->Host(), url->App());

	auto app			= std::static_pointer_cast<OvtApplication>(GetApplicationByName(vhost_app_name));
	if (app == nullptr)
	{
		ov::String msg;
		msg.Format("There is no such app (%s)", vhost_app_name.CStr());
		ResponseResult(remote, 0, "subscribe", request_id, 404, msg);
		return;
	}

	auto stream = std::static_pointer_cast<OvtStream>(app->GetStream(url->Stream()));
	if (stream == nullptr)
	{
		ov::String msg;
		msg.Format("There is no such stream (%s/%s)", vhost_app_name.CStr(), url->Stream().CStr());
		ResponseResult(remote, 0, "subscribe", request_id, 404, msg);
		return;
	}

	auto session = std::dynamic_pointer_cast<OvtSession>(stream->GetSession(remote->GetNativeHandle()));
	if (session == nullptr)
	{
		ResponseResult(remote, 0, "subscribe", request_id, 404, "There is no active play session for subscribe");
		return;
	}

	bool full_stream = false;
	std::optional<std::set<int32_t>> allowed_track_ids;
	if (ovt_pub::internal::ParseSubscribeSelection(contents, full_stream, allowed_track_ids) == false)
	{
		ResponseResult(remote, session->GetId(), "subscribe", request_id, 400, "Invalid subscribe contents");
		return;
	}

	if (allowed_track_ids.has_value())
	{
		for (auto track_id : *allowed_track_ids)
		{
			if (stream->GetTrack(track_id) == nullptr)
			{
				ov::String msg;
				msg.Format("Track id is not valid: %d", track_id);
				ResponseResult(remote, session->GetId(), "subscribe", request_id, 404, msg);
				return;
			}
		}
	}

	auto session_scope_url = ovt_pub::internal::ResolveCanonicalSubscribeScopeUrl(url, stream, allowed_track_ids, full_stream);
	if (session_scope_url == nullptr)
	{
		ResponseResult(remote, session->GetId(), "subscribe", request_id, 400, "Subscribe track selection must match full stream or a union of known playlists");
		return;
	}

	session->UpdateAllowedTrackIds(full_stream ? std::nullopt : allowed_track_ids);
	ovt_pub::internal::StoreRequestScopeOnSession(session, session_scope_url);
	stream->RefreshSessionScope(session);

	ResponseResult(remote, session->GetId(), "subscribe", request_id, 200, "ok");
}

void OvtPublisher::HandleStopRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, uint32_t request_id, const std::shared_ptr<const ov::Url> &url)
{
	auto vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(url->Host(), url->App());
	auto stream = std::static_pointer_cast<OvtStream>(GetStream(vhost_app_name, url->Stream()));

	if (stream == nullptr)
	{
		ov::String msg;
		msg.Format("There is no such stream (%s/%s)", vhost_app_name.CStr(), url->Stream().CStr());
		ResponseResult(remote, 0, "stop", request_id, 404, msg);
		return;
	}

	ResponseResult(remote, session_id, "stop", request_id, 200, "ok");

	// Session ID is remote socket's ID
	stream->RemoveSession(remote->GetNativeHandle());
}

void OvtPublisher::ResponseResult(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String app, uint32_t request_id, uint32_t code, const ov::String &msg)
{
	Json::Value root;

	root["id"] = request_id;
	root["application"] = app.CStr();
	root["code"] = code;
	root["message"] = msg.CStr();

	SendResponse(remote, session_id, ov::Json::Stringify(root));
}

void OvtPublisher::ResponseResult(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, ov::String app, uint32_t request_id, uint32_t code, const ov::String &msg, const Json::Value &contents)
{
	Json::Value root;

	root["id"] = request_id;
	root["application"] = app.CStr();
	root["code"] = code;
	root["message"] = msg.CStr();
	root["contents"] = contents;

	SendResponse(remote, session_id, ov::Json::Stringify(root));
}

void OvtPublisher::SendResponse(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String &payload)
{
	OvtPacketizer packetizer;

	if (packetizer.PacketizeMessage(OVT_PAYLOAD_TYPE_MESSAGE_RESPONSE, ov::Clock::NowMSec(), payload.ToData(false)) == false)
	{
		return;
	}

	while (packetizer.IsAvailablePackets())
	{
		auto packet = packetizer.PopPacket();
		if (packet == nullptr)
		{
			return;
		}

		remote->Send(packet->GetData());
	}
}

bool OvtPublisher::LinkRemoteWithStream(int remote_id, std::shared_ptr<OvtStream> &stream)
{
	// For ungraceful disconnect
	// one remote id can be join multiple streams.
	std::lock_guard<std::shared_mutex> guard(_remote_stream_map_lock);
	_remote_stream_map.insert(std::pair<int, std::shared_ptr<OvtStream>>(remote_id, stream));

	return true;
}

bool OvtPublisher::UnlinkRemoteFromStream(int remote_id)
{
	std::lock_guard<std::shared_mutex> guard(_remote_stream_map_lock);
	_remote_stream_map.erase(remote_id);

	return true;
}
