#pragma once

#include <orchestrator/orchestrator.h>

#include "base/common_types.h"
#include "base/mediarouter/mediarouter_application_interface.h"
#include "base/ovlibrary/url.h"
#include "base/publisher/publisher.h"
#include "modules/ovt_packetizer/ovt_depacketizer.h"
#include "modules/ovt_packetizer/ovt_packet.h"
#include "ovt_application.h"
#include "ovt_track_filter.h"

#include <optional>
#include <utility>

class OvtPublisher : public pub::Publisher, public PhysicalPortObserver
{
public:
	static std::shared_ptr<OvtPublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

	OvtPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
	~OvtPublisher() override;
	bool Start() override;
	bool Stop() override;

private:
	// Per-connection state of an edge.
	// `is_ovt2` follows the latest request: a request without the `ovt` object is OVT1,
	// even when an earlier request on the same connection carried one.
	struct RemoteContext
	{
		std::shared_ptr<OvtDepacketizer> depacketizer;
		bool is_ovt2		  = false;
		// The edge's `ovt.version`, its generation. 0 when it declared none, which an OVT1 edge never does.
		// Nothing decides on it yet; it is here so a later release can hold back what this
		// generation mishandles without refusing the request.
		uint32_t edge_version = 0;
		// The generation last written to the log. Both follow the latest request, so the line is
		// repeated whenever the pair changes rather than pinned to the first request.
		std::optional<std::pair<bool, uint32_t>> announced;
		// One log per connection for media a peer sent on a direction that takes none,
		// and one for a message of a payload type an origin does not take
		bool media_dropped			 = false;
		bool unknown_message_dropped = false;
		// What this connection described last: the stream, the instance of it, and its track epoch then.
		// The play that follows hands the epoch to its session, which uses it to spot a NOTIFY that went out
		// while the session was still behind the send gate.
		// A different key is another stream, which a connection may play.
		// The same key with another id is that stream recreated, which the play refuses.
		ov::String described_stream_key;
		std::optional<info::stream_id_t> described_stream_id;
		uint32_t track_epoch = 0;
	};

	// Records which stream this connection described and its track epoch at that moment,
	// once the response is on its way
	void RememberDescribedStream(const std::shared_ptr<ov::Socket> &remote, const ov::String &stream_key, info::stream_id_t stream_id, uint32_t track_epoch);

	//--------------------------------------------------------------------
	// Implementation of Publisher
	//--------------------------------------------------------------------
	PublisherType GetPublisherType() const override
	{
		return PublisherType::Ovt;
	}
	const char *GetPublisherName() const override
	{
		return "OVTPublisher";
	}

	bool OnCreateHost(const info::Host &host_info) override;
	bool OnDeleteHost(const info::Host &host_info) override;
	std::shared_ptr<pub::Application> OnCreatePublisherApplication(const info::Application &application_info) override;
	bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) override;

	//--------------------------------------------------------------------

	//--------------------------------------------------------------------
	// Implementation of PhysicalPortObserver
	//--------------------------------------------------------------------
	void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
	void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override;
	void OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error) override;
	//--------------------------------------------------------------------

	// Reads the publisher configuration through the `cfg` getters, which throw on a member
	// the config tree no longer holds
	void HandleDescribeRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t request_id, const std::shared_ptr<const ov::Url> &url);
	// The filter one edge's request resolves to:
	// OVT1 or OVT2 from the connection, TrackSet from the URL, and, on a play, the edge's own selection.
	// Returns false when the URL names a TrackSet the stream does not have.
	bool ResolveTrackFilter(const std::shared_ptr<ov::Socket> &remote, const std::shared_ptr<OvtStream> &stream, const ov::String &track_set_name, const std::optional<std::set<uint32_t>> &requested_track_ids, OvtTrackFilter &filter);
	void SendPlayResponse(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, uint32_t request_id,
						  const std::shared_ptr<OvtStream> &stream, const std::optional<std::set<uint32_t>> &allowed_track_ids);
	void HandlePlayRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t request_id, const std::shared_ptr<const ov::Url> &url, const std::optional<std::set<uint32_t>> &requested_track_ids);
	void HandleStopRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, uint32_t request_id, const std::shared_ptr<const ov::Url> &url);

	void ResponseResult(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String app, uint32_t request_id, uint32_t code, const ov::String &msg);
	void ResponseResult(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String app, uint32_t request_id, uint32_t code, const ov::String &msg, const Json::Value &contents);

	void SendResponse(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String &payload);

	bool LinkRemoteWithStream(int remote_id, std::shared_ptr<OvtStream> &stream);
	bool UnlinkRemoteFromStream(int remote_id);
	void RemoveSessions(int remote_id);

	std::shared_ptr<RemoteContext> GetRemoteContext(int remote_id);
	bool RemoveRemoteContext(int remote_id);
	// Adds the origin's `ovt` object to a response going to an OVT2 edge
	void AppendOvtObject(const std::shared_ptr<ov::Socket> &remote, Json::Value &root);

	std::mutex _server_port_list_mutex;
	std::vector<std::shared_ptr<PhysicalPort>> _server_port_list;

	// remote id : per-connection state
	std::mutex _remotes_lock;
	std::map<int, std::shared_ptr<RemoteContext>> _remotes;
	// When a client is disconnected ungracefully, this map helps to find stream and delete the session quickly
	std::multimap<int, std::shared_ptr<OvtStream>> _remote_stream_map;
	std::shared_mutex _remote_stream_map_lock;
};
