#pragma once

#include <orchestrator/orchestrator.h>

#include "base/common_types.h"
#include "base/mediarouter/mediarouter_application_interface.h"
#include "base/ovlibrary/url.h"
#include "base/publisher/publisher.h"
#include "modules/ovt_packetizer/ovt_depacketizer.h"
#include "modules/ovt_packetizer/ovt_packet.h"
#include "ovt_application.h"

class OvtPublisher : public pub::Publisher, public PhysicalPortObserver
{
public:
	static std::shared_ptr<OvtPublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

	OvtPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
	~OvtPublisher() override;
	bool Stop() override;

private:
	bool Start() override;

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

	void HandleDescribeRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t request_id, const std::shared_ptr<const ov::Url> &url);
	void HandlePlayRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t request_id, const std::shared_ptr<const ov::Url> &url);
	void HandleStopRequest(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, uint32_t request_id, const std::shared_ptr<const ov::Url> &url);

	void ResponseResult(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String app, uint32_t request_id, uint32_t code, const ov::String &msg);
	void ResponseResult(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String app, uint32_t request_id, uint32_t code, const ov::String &msg, const Json::Value &contents);

	void SendResponse(const std::shared_ptr<ov::Socket> &remote, uint32_t session_id, const ov::String &payload);

	bool LinkRemoteWithStream(int remote_id, std::shared_ptr<OvtStream> &stream);
	bool UnlinkRemoteFromStream(int remote_id);

	std::shared_ptr<OvtDepacketizer> GetDepacketizer(int remote_id);
	bool RemoveDepacketizer(int remote_id);

	std::mutex _server_port_list_mutex;
	std::vector<std::shared_ptr<PhysicalPort>> _server_port_list;

	// remote id : depacketizer
	std::mutex _depacketizers_lock;
	std::map<int, std::shared_ptr<OvtDepacketizer>> _depacketizers;
	// When a client is disconnected ungracefully, this map helps to find stream and delete the session quickly
	std::multimap<int, std::shared_ptr<OvtStream>> _remote_stream_map;
	std::shared_mutex _remote_stream_map_lock;
};
