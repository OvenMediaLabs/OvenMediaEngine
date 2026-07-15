//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/http/server/http_server.h>
#include <modules/http/server/https_server.h>
#include <modules/http/server/http_request_interceptors.h>

// Peer ID of OME
#define P2P_OME_PEER_ID                             0
#define P2P_INVALID_PEER_ID                         -1

typedef int peer_id_t;

enum class RtcOsType : char
{
	// PC

	Windows,
	macOS,
	Linux,

	// Mobile

	Android,
	iOS,

	Other
};

enum class RtcBrowserType : uint16_t
{
	Chrome,
	Firefox,
	Safari,
	Edge,
	Other
};

struct RtcVersion
{
	int first = 0;
	int second = 0;
	int third = 0;
	int fourth = 0;

	ov::String version;

	explicit RtcVersion(int first)
		: first(first)
	{
		version.Format("%d", first);
	}

	RtcVersion(int first, int second)
		: first(first),
		  second(second)
	{
		version.Format("%d.%d", first, second);
	}

	RtcVersion(int first, int second, int third)
		: first(first),
		  second(second),
		  third(third)
	{
		version.Format("%d.%d.%d", first, second, third);
	}

	RtcVersion(int first, int second, int third, int fourth)
		: first(first),
		  second(second),
		  third(third),
		  fourth(fourth)
	{
		version.Format("%d.%d.%d.%d", first, second, third, fourth);
	}

	explicit RtcVersion(const ov::String &version)
	{
		this->version = version;

		auto tokens = version.Split(".");

		switch(tokens.size())
		{
			case 4:
				fourth = ov::Converter::ToInt32(tokens[3]);

			case 3:
				third = ov::Converter::ToInt32(tokens[2]);

			case 2:
				second = ov::Converter::ToInt32(tokens[1]);

			case 1:
				first = ov::Converter::ToInt32(tokens[0]);

			default:
				break;
		}
	}
};

struct RtcPeerBrowser
{
	RtcOsType os_type = RtcOsType::Other;
	RtcVersion os_version;

	RtcBrowserType browser_type = RtcBrowserType::Other;
	RtcVersion browser_version;

	RtcPeerBrowser()
		: os_version(0),
		  browser_version(0)
	{
	}

	bool IsMobile() const
	{
		return (os_type == RtcOsType::iOS) || (os_type == RtcOsType::Android);
	}

	ov::String ToString() const
	{
		ov::String description;

		switch(browser_type)
		{
			case RtcBrowserType::Chrome:
				description.Append("Chrome v");
				break;

			case RtcBrowserType::Firefox:
				description.Append("Firefox v");
				break;

			case RtcBrowserType::Safari:
				description.Append("Safari v");
				break;

			case RtcBrowserType::Edge:
				description.Append("Edge v");
				break;

			case RtcBrowserType::Other:
				description.Append("Other v");
				break;
		}

		description.Append(browser_version.version);

		switch(os_type)
		{
			case RtcOsType::Windows:
				description.Append(" on Windows v");
				break;

			case RtcOsType::macOS:
				description.Append(" on macOS v");
				break;

			case RtcOsType::Linux:
				description.Append(" on Linux v");
				break;

			case RtcOsType::Android:
				description.Append(" on Android v");
				break;

			case RtcOsType::iOS:
				description.Append(" on iOS v");
				break;

			case RtcOsType::Other:
				description.Append(" on Other v");
				break;
		}

		description.Append(os_version.version);

		return description;
	}
};

class RtcPeerInfo
{
protected:
	struct PrivateToken
	{
	};

public:
	friend class RtcP2PManager;

	explicit RtcPeerInfo(PrivateToken token)
	{
	}

	~RtcPeerInfo() = default;

	peer_id_t GetId() const
	{
		return _id;
	}

	bool IsHost() const
	{
		return _is_host;
	}

	void MakeAsHost()
	{
		_is_host = true;
	}

	bool CanAccept() const
	{
		return _can_accept;
	}

	const RtcPeerBrowser &GetBrowser() const
	{
		return _browser;
	}

	std::shared_ptr<RtcPeerInfo> GetHostPeer()
	{
		return _host_peer;
	}

	std::shared_ptr<http::svr::ws::WebSocketSession> GetSession()
	{
		return _ws_session;
	}

	bool IsCompatibleWith(const std::shared_ptr<RtcPeerInfo> &peer);

	ov::String ToString() const;

	static std::shared_ptr<RtcPeerInfo> FromUserAgent(peer_id_t id, const ov::String &user_agent, const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session);

protected:
	RtcPeerInfo() = default;
	static RtcPeerBrowser ParseBrowserInfo(const ov::String &user_agent);

	// peer id
	peer_id_t _id = 0;

	// Indicates whether this is a host
	bool _is_host = false;
	// Indicates whether a client peer can be accepted
	bool _can_accept = false;

	// OS & browser type
	RtcPeerBrowser _browser;

	// host peer info (client only)
	std::shared_ptr<RtcPeerInfo> _host_peer;

	std::shared_ptr<http::svr::ws::WebSocketSession> _ws_session;

	std::map<peer_id_t, std::shared_ptr<RtcPeerInfo>> _client_list;
};
