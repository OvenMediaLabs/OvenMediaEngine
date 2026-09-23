#include "ovt_publisher.h"

#include <modules/ovt_packetizer/ovt_signaling.h>

#include <base/ovlibrary/url.h>

#include "ovt_private.h"
#include "ovt_session.h"

// A `<Required>` block with no `<MediaType>` in it is the empty set.
// Without that distinction the member defaults (`Video`, `Audio`) survive
// and an operator has no way to empty the describe list.
// It only reaches the `ovt.required` array of a describe. The other channel, the PT 40 set a session
// learns at packetization, is built from what the media actually carries and has no setting.
// The media types whose codecs an edge has to know to take the stream.
// What an edge must understand follows from what this build can send, so it is fixed here
// rather than configured: an operator has no way to know which codecs a stream will carry.
static const std::set<cmn::MediaType> REQUIRED_MEDIA_TYPES = {cmn::MediaType::Video, cmn::MediaType::Audio};

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

std::shared_ptr<OvtPublisher::RemoteContext> OvtPublisher::GetRemoteContext(int remote_id)
{
	std::lock_guard<std::mutex> guard(_remotes_lock);

	auto it = _remotes.find(remote_id);
	if (it != _remotes.end())
	{
		return it->second;
	}

	auto context		  = std::make_shared<RemoteContext>();
	context->depacketizer = std::make_shared<OvtDepacketizer>(OvtDepacketizer::MediaRole::MessagesOnly);
	_remotes[remote_id]	  = context;

	return context;
}

bool OvtPublisher::RemoveRemoteContext(int remote_id)
{
	std::lock_guard<std::mutex> guard(_remotes_lock);
	_remotes.erase(remote_id);
	return true;
}

void OvtPublisher::AppendOvtObject(const std::shared_ptr<ov::Socket> &remote, Json::Value &root)
{
	if (GetRemoteContext(remote->GetNativeHandle())->is_ovt2)
	{
		root["ovt"] = ovt::MakeResponseOvtObject();
	}
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
	auto context = GetRemoteContext(remote->GetNativeHandle());
	auto depacketizer = context->depacketizer;

	if (depacketizer->AppendPacket(data) == false)
	{
		// The depacketizer does not consume bytes it cannot parse, so an unparsable connection is closed.
		ResponseResult(remote, 0, "unknown", 0, 500, "Server Internals Error");
		RemoveSessions(remote->GetNativeHandle());
		remote->Close();
		return;
	}

	// An origin never receives media, so its depacketizer discards a media fragment as it is parsed
	// rather than reassembling one it is certain to throw away. Nothing reaches the queue, so the count
	// is the only way to see that a peer sent any.
	if ((context->media_dropped == false) && (depacketizer->GetDroppedMediaFragmentCount() > 0))
	{
		context->media_dropped = true;
		logtw(
			"Dropped at least %zu media fragment(s) from %s; an origin only takes requests. "
			"Further occurrences will be suppressed",
			depacketizer->GetDroppedMediaFragmentCount(), remote->ToString().CStr());
	}

	while (true)
	{
		// Empty when the queue has no whole message left
		auto message = depacketizer->PopMessage();
		if (message.has_value() == false)
		{
			break;
		}

		// An origin only takes requests
		if (message->payload_type != OvtPayloadType::MessageRequest)
		{
			if (context->unknown_message_dropped == false)
			{
				context->unknown_message_dropped = true;
				logtw("Ignored a message with payload type %u from %s", ov::ToUnderlyingType(message->payload_type), remote->ToString().CStr());
			}

			continue;
		}

		// Parsing Payload.
		// `Parse()` throws on a payload the parser refuses, and nesting past jsoncpp's stack limit
		// is reachable from one packet, so the throw is answered like any other malformed request.
		ov::String payload(message->data->GetDataAs<char>(), message->data->GetLength());
		bool parse_failed	  = false;
		ov::JsonObject object = [&]() {
			try
			{
				return ov::Json::Parse(payload);
			}
			catch (const Json::Exception &e)
			{
				logtw("An invalid request from %s : %s", remote->ToString().CStr(), e.what());
				parse_failed = true;
				return ov::JsonObject();
			}
		}();

		// A rejected request does not end the drain: one `AppendPacket()` can queue several messages,
		// and nothing else pops them, so returning here would leave a valid request behind this one
		// unanswered until more bytes happen to arrive.
		if (parse_failed || object.IsNull() || (object.GetJsonValue().isObject() == false))
		{
			ResponseResult(remote, 0, "unknown", 0, 404, "An invalid request : Json format");
			continue;
		}

		// OVT1 detection: a request without the `ovt` object is an OVT1 edge.
		// No other field is used to guess, and every request is judged on its own.
		const Json::Value &root = object.GetJsonValue();

		if (ovt::HasNonObjectOvt(root))
		{
			ResponseResult(remote, 0, "unknown", 0, 404, "An invalid request : ovt must be an object");
			continue;
		}

		context->is_ovt2		= ovt::HasOvtObject(root);
		// Kept, never judged on. A later release reads it to hold back what this generation of edge
		// mishandles; refusing on it would let this origin decide for a build that knows more than it.
		context->edge_version	= ovt::GetPeerVersion(root);

		const auto announcement = std::make_pair(context->is_ovt2, context->edge_version);
		if (context->announced != announcement)
		{
			// Only this one log line reads it, so it does not outlive the request
			auto agent		   = (context->is_ovt2 && root["ovt"]["agent"].isString())
									 ? ovt::SanitizeForLog(root["ovt"]["agent"].asString().c_str())
									 : ov::String();

			context->announced = announcement;
			logti("OVT edge %s speaks %s (generation %u)%s%s", remote->ToString().CStr(),
				  context->is_ovt2 ? "OVT2" : "OVT1", context->edge_version,
				  agent.IsEmpty() ? "" : ", agent: ", agent.CStr());
		}

		Json::Value &json_request_id = object.GetJsonValue()["id"];
		Json::Value &json_request_app = object.GetJsonValue()["application"];
		Json::Value &json_request_target = object.GetJsonValue()["target"];

		if (json_request_id.isNull() || !json_request_id.isUInt() ||
			json_request_app.isNull() || !json_request_app.isString() ||
			json_request_target.isNull() || !json_request_target.isString())
		{
			ResponseResult(remote, 0, "unknown", 0, 404, "An invalid request : id or target or application are invalid");
			continue;
		}

		uint32_t request_id = json_request_id.asUInt();
		ov::String app = json_request_app.asString().c_str();
		auto url = ov::Url::Parse(json_request_target.asString().c_str());
		if (url == nullptr)
		{
			ResponseResult(remote, 0, "unknown", json_request_id.asUInt(), 404, "An invalid request : Target is not valid");
			continue;
		}

		if (ovt::IsApplication(app, ovt::APPLICATION_DESCRIBE))
		{
			HandleDescribeRequest(remote, request_id, url);
		}
		else if (ovt::IsApplication(app, ovt::APPLICATION_PLAY))
		{
			// The edge names the tracks it will register. It can only do that after seeing a
			// describe, so this is the one message that carries a selection. An OVT1 edge names none.
			size_t ignored			 = 0;
			auto requested_track_ids = context->is_ovt2
										   ? ovt::ParseTrackIdArray(root["ovt"]["trackIds"], &ignored)
										   : std::nullopt;
			if (ignored > 0)
			{
				logtw("Ignored %zu non-integer entries in the play request's trackIds from %s",
					  ignored, remote->ToString().CStr());
			}

			HandlePlayRequest(remote, request_id, url, requested_track_ids);
		}
		else if (ovt::IsApplication(app, ovt::APPLICATION_STOP))
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
		RemoveSessions(remote->GetNativeHandle());
	}
	UnlinkRemoteFromStream(remote->GetNativeHandle());
	RemoveRemoteContext(remote->GetNativeHandle());
}

void OvtPublisher::RemoveSessions(int remote_id)
{
	std::shared_lock<std::shared_mutex> lock(_remote_stream_map_lock);
	auto streams = _remote_stream_map.equal_range(remote_id);
	for (auto it = streams.first; it != streams.second; ++it)
	{
		auto stream = it->second;
		stream->RemoveSessionByConnectorId(remote_id);
	}
}

bool OvtPublisher::ResolveTrackFilter(const std::shared_ptr<ov::Socket> &remote, const std::shared_ptr<OvtStream> &stream, const ov::String &track_set_name, const std::optional<std::set<uint32_t>> &requested_track_ids, OvtTrackFilter &filter)
{
	filter.legacy_edge		   = (GetRemoteContext(remote->GetNativeHandle())->is_ovt2 == false);
	filter.requested_track_ids = requested_track_ids;

	if (track_set_name.IsEmpty() == false)
	{
		std::set<uint32_t> track_ids;
		if (stream->ResolveTrackSetTrackIds(track_set_name, track_ids) == false)
		{
			return false;
		}

		filter.track_set_ids = std::move(track_ids);
	}

	return true;
}

// The PLAY response tells an OVT2 edge exactly which tracks it will receive,
// so what the edge registers is always a subset of what is sent,
// even when a track change landed between describe and play.
// A session that lost a track also gets its playlists rebuilt for that same set: the describe's copy
// was written before the edge made its selection, so only this one has renditions that match the
// tracks the edge ends up with. A session that kept every track keeps the describe's copy.
void OvtPublisher::SendPlayResponse(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, uint32_t request_id,
									const std::shared_ptr<OvtStream> &stream, const std::optional<std::set<uint32_t>> &allowed_track_ids)
{
	Json::Value root;
	root["id"]			= request_id;
	root["application"] = ovt::APPLICATION_PLAY;
	root["code"]		= 200;
	root["message"]		= "ok";
	AppendOvtObject(remote, root);

	if (root.isMember("ovt"))
	{
		std::set<uint32_t> every_track_id;
		for (const auto &[track_id, track] : stream->GetTracks())
		{
			every_track_id.insert(static_cast<uint32_t>(track_id));
		}

		const auto confirmed = allowed_track_ids.value_or(every_track_id);

		Json::Value ids(Json::arrayValue);
		for (auto track_id : confirmed)
		{
			ids.append(track_id);
		}
		root["ovt"]["allowedTrackIds"] = ids;

		// Only a session that lost a track needs its renditions rebuilt. Sending a rebuilt set to a
		// session that kept everything would replace the describe's playlists with ones that went
		// through the rendition pruning for nothing.
		if (confirmed != every_track_id)
		{
			root["ovt"]["playlists"] = stream->BuildPlaylistsForTracks(confirmed);
		}

		if (ids.empty())
		{
			root["ovt"]["reason"] = ovt::REASON_NO_DELIVERABLE_TRACK;
		}
	}

	SendResponse(remote, session_id, ov::Json::Stringify(root));
}

void OvtPublisher::HandleDescribeRequest(const std::shared_ptr<ov::Socket> &remote, const uint32_t request_id, const std::shared_ptr<const ov::Url> &url)
{
	auto orchestrator = ocst::Orchestrator::GetInstance();

	auto host_name = url->Host();
	auto app_name = url->App();
	auto vhost_app_name = orchestrator->ResolveApplicationNameFromDomain(host_name, app_name);
	auto stream_name = url->Stream();
	// The optional 3rd path segment selects a TrackSet on the stream:
	//   ovt://host/app/stream/<trackset>
	auto track_set_name = url->File();
	ov::String msg;

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

	// A describe carries no selection: the edge has not seen the track list yet.
	OvtTrackFilter filter;
	if (ResolveTrackFilter(remote, stream, track_set_name, std::nullopt, filter) == false)
	{
		msg.Format("(%s/%s) stream has no TrackSet named [%s].",
				   vhost_app_name.CStr(), url->Stream().CStr(), track_set_name.CStr());
		ResponseResult(remote, 0, "describe", request_id, 404, msg);
		return;
	}

	// Read before the description is built. A change that lands in between then shows up as a
	// difference the session catches up on; the other order could hide a change the description missed.
	// It is recorded only once the response is on its way, so a failed describe leaves no baseline.
	const auto described_epoch = stream->GetTrackEpoch();
	const auto stream_key	   = ov::String::FormatString("%s/%s", vhost_app_name.CStr(), url->Stream().CStr());

	Json::Value description;
	bool filtered = false;
	if (stream->GetDescription(filter, description, &filtered) == false)
	{
		msg.Format("(%s/%s) stream doesn't have description.", vhost_app_name.CStr(), url->Stream().CStr());
		ResponseResult(remote, 0, "describe", request_id, 404, msg);
		return;
	}

	// An empty track set answers 200 with an empty `tracks`, which is what v0.21.0.0 did.
	// An OVT2 edge is told why in `ovt.reason` below and stops instead of waiting for media.
	const bool no_deliverable_track = description["stream"]["tracks"].empty();
	if (no_deliverable_track)
	{
		logtw("(%s/%s) has no track this edge can receive", vhost_app_name.CStr(), url->Stream().CStr());
	}

	if (filter.legacy_edge)
	{
		// An OVT1 edge ignores the keys it does not know, so the OVT2 form goes out as it is.
		// Only the index hints need rewriting, and only when filtering moved a track out of its group.
		if (filtered)
		{
			OvtStream::RenumberIndexHints(description["stream"]);
		}
		RememberDescribedStream(remote, stream_key, stream->GetId(), described_epoch);
		ResponseResult(remote, 0, "describe", request_id, 200, "ok", description);
		return;
	}

	// The required set names the codecs of the tracks the edge is about to see;
	// an edge that does not know one of them refuses the stream before any media flows
	auto required = OvtStream::CollectRequiredTokens(description, REQUIRED_MEDIA_TYPES);

	Json::Value root;
	root["id"]			= request_id;
	root["application"] = ovt::APPLICATION_DESCRIBE;
	root["code"]		= 200;
	root["message"]		= "ok";
	root["contents"]	= description;
	AppendOvtObject(remote, root);
	if (no_deliverable_track)
	{
		root["ovt"]["reason"] = ovt::REASON_NO_DELIVERABLE_TRACK;
	}
	root["ovt"]["required"] = Json::Value(Json::arrayValue);
	for (const auto &token : required)
	{
		root["ovt"]["required"].append(token.CStr());
	}

	RememberDescribedStream(remote, stream_key, stream->GetId(), described_epoch);
	SendResponse(remote, 0, ov::Json::Stringify(root));
}

void OvtPublisher::RememberDescribedStream(const std::shared_ptr<ov::Socket> &remote, const ov::String &stream_key, info::stream_id_t stream_id, uint32_t track_epoch)
{
	auto context				  = GetRemoteContext(remote->GetNativeHandle());
	context->described_stream_key = stream_key;
	context->described_stream_id  = stream_id;
	context->track_epoch		  = track_epoch;
}

void OvtPublisher::HandlePlayRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t request_id, const std::shared_ptr<const ov::Url> &url, const std::optional<std::set<uint32_t>> &requested_track_ids)
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

	// The describe and this play can land on two different instances of the same stream.
	// The edge takes its tracks and playlists from the describe, so a changed instance is refused.
	auto stream_key = ov::String::FormatString("%s/%s", vhost_app_name.CStr(), url->Stream().CStr());
	auto context	= GetRemoteContext(remote->GetNativeHandle());
	if (context->described_stream_id.has_value() && (context->described_stream_key == stream_key) &&
		(*context->described_stream_id != stream->GetId()))
	{
		ov::String msg;
		msg.Format("(%s) was recreated after this connection described it", stream_key.CStr());
		logtw("%s. The edge has to describe it again", msg.CStr());
		ResponseResult(remote, 0, "play", request_id, 404, msg);
		return;
	}

	// Optional TrackSet selector (3rd path segment), composed with the OVT1 codec removal
	auto track_set_name = url->File();
	OvtTrackFilter filter;
	if (ResolveTrackFilter(remote, stream, track_set_name, requested_track_ids, filter) == false)
	{
		ov::String msg;
		msg.Format("(%s/%s) stream has no TrackSet named [%s].",
				   vhost_app_name.CStr(), url->Stream().CStr(), track_set_name.CStr());
		ResponseResult(remote, 0, "play", request_id, 404, msg);
		return;
	}

	// The set the session will actually deliver.
	// Describe and play select independently, so a track change in between can empty the set only now.
	// An empty set still answers 200 and still creates the session, as v0.21.0.0 did;
	// `SendPlayResponse()` puts the reason in the `ovt` object for an OVT2 edge.
	auto allowed_track_ids = stream->ResolveAllowedTrackIds(filter);
	if (allowed_track_ids.has_value() && allowed_track_ids->empty())
	{
		logtw("(%s/%s) has no track this edge can receive", vhost_app_name.CStr(), url->Stream().CStr());
	}

	// Session ID is remote socket's ID
	auto session = OvtSession::Create(app, stream, remote->GetNativeHandle(), remote);
	if (session == nullptr)
	{
		ov::String msg;
		msg.Format("Internal Error : Cannot create session");
		ResponseResult(remote, 0, "play", request_id, 404, msg);
		return;
	}

	// Only this connection's describe of this stream says what the edge already knows
	auto described_epoch = (context->described_stream_id == stream->GetId())
							   ? std::optional<uint32_t>(context->track_epoch)
							   : std::nullopt;
	session->SetPeer(context->is_ovt2, described_epoch);

	if (allowed_track_ids.has_value())
	{
		session->SetAllowedTrackIds(*allowed_track_ids);
		logti("OVT session connected with %zu of the tracks (TrackSet [%s], %s edge, stream %s/%s)",
			  allowed_track_ids->size(), track_set_name.CStr(), filter.legacy_edge ? "OVT1" : "OVT2",
			  vhost_app_name.CStr(), url->Stream().CStr());
	}

	LinkRemoteWithStream(remote->GetNativeHandle(), stream);

	SendPlayResponse(remote, session->GetId(), request_id, stream, allowed_track_ids);

	stream->AddSession(session);
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

	// Spelled out with the direction: the origin's own stop notification uses the same message name
	logti("Edge %s requested stop of %s/%s", remote->ToString().CStr(), vhost_app_name.CStr(), url->Stream().CStr());
	ResponseResult(remote, session_id, "stop", request_id, 200, "ok");

	// Session ID is remote socket's ID
	stream->RemoveSession(remote->GetNativeHandle());
}

void OvtPublisher::ResponseResult(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String app, uint32_t request_id, uint32_t code, const ov::String &msg)
{
	Json::Value root;

	root["id"] = request_id;
	root["application"] = ovt::TruncateForResponse(app).CStr();
	root["code"] = code;
	root["message"] = ovt::TruncateForResponse(msg).CStr();
	AppendOvtObject(remote, root);

	SendResponse(remote, session_id, ov::Json::Stringify(root));
}

void OvtPublisher::ResponseResult(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, ov::String app, uint32_t request_id, uint32_t code, const ov::String &msg, const Json::Value &contents)
{
	Json::Value root;

	root["id"] = request_id;
	root["application"] = ovt::TruncateForResponse(app).CStr();
	root["code"] = code;
	root["message"] = ovt::TruncateForResponse(msg).CStr();
	root["contents"] = contents;
	AppendOvtObject(remote, root);

	SendResponse(remote, session_id, ov::Json::Stringify(root));
}

void OvtPublisher::SendResponse(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String &payload)
{
	OvtPacketizer packetizer;

	if (packetizer.PacketizeMessage(OvtPayloadType::MessageResponse, ov::Clock::NowMSec(), payload.ToData(false)) == false)
	{
		return;
	}

	// The whole response goes out in one `Send()` even when it is fragmented.
	// A socket write is queued whole, but the stream worker writes media and notifications to the same
	// socket, and an edge appends consecutive message fragments into one buffer: a packet landing
	// between two fragments would be glued onto this response and fail its JSON parse.
	auto response = std::make_shared<ov::Data>();
	while (packetizer.IsAvailablePackets())
	{
		auto packet = packetizer.PopPacket();
		if (packet == nullptr)
		{
			return;
		}

		response->Append(packet->GetData());
	}

	remote->Send(response);
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
