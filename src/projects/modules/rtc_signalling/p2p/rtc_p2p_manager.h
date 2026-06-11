//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "rtc_peer_info.h"

class RtcP2PManager
{
public:
	RtcP2PManager(const cfg::Server &server_config);

	// Create a PeerInfo from user-agent
	std::shared_ptr<RtcPeerInfo> CreatePeerInfo(peer_id_t id, const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session);

	// Add to _peer_list
	std::shared_ptr<RtcPeerInfo> FindPeer(peer_id_t peer_id);
	bool RemovePeer(const std::shared_ptr<RtcPeerInfo> &peer);

	bool RegisterAsHostPeer(const std::shared_ptr<RtcPeerInfo> &peer);
	std::shared_ptr<RtcPeerInfo> TryToRegisterAsClientPeer(const std::shared_ptr<RtcPeerInfo> &peer);

	std::shared_ptr<RtcPeerInfo> GetClientPeerOf(const std::shared_ptr<RtcPeerInfo> &host, peer_id_t client_id);
	std::map<peer_id_t, std::shared_ptr<RtcPeerInfo>> GetClientPeerList(const std::shared_ptr<RtcPeerInfo> &host);

	int GetPeerCount() const;
	int GetClientPeerCount() const;

	bool IsEnabled() const
	{
		return _is_enabled;
	}

protected:
	bool _is_enabled = false;

	int _max_client_peers_per_host_peer = -1;

	ov::RecursiveMutex _list_mutex;

	// All peer list
	// key: peer id, value: peer info list
	std::map<peer_id_t, std::shared_ptr<RtcPeerInfo>> _peer_list OV_GUARDED_BY(_list_mutex);

	// List of hosts that can accept client
	// key: host id, value: host info
	std::map<peer_id_t, std::shared_ptr<RtcPeerInfo>> _available_list OV_GUARDED_BY(_list_mutex);

	int _total_client_count OV_GUARDED_BY(_list_mutex) = 0;
};
