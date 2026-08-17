//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================

#include "rtc_peer_info.h"

std::shared_ptr<RtcPeerInfo> RtcPeerInfo::FromUserAgent(peer_id_t id, const ov::String &user_agent, const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session)
{
	RtcPeerBrowser browser = ParseBrowserInfo(user_agent);

	auto peer_info = std::make_shared<RtcPeerInfo>((PrivateToken){});

	peer_info->_id = id;
	peer_info->_browser = browser;
	peer_info->_ws_session = ws_session;

	peer_info->_can_accept =
		(
			(browser.IsMobile() == false) &&
			(browser.os_type != RtcOsType::Linux)
		) &&
		!((browser.browser_type == RtcBrowserType::Edge) ||
		  (browser.browser_type == RtcBrowserType::Other));

	return peer_info;
}

RtcPeerBrowser RtcPeerInfo::ParseBrowserInfo(const ov::String &user_agent)
{
	RtcPeerBrowser browser;

	// Detect OS type
	if(user_agent.IndexOf("Mobile") >= 0)
	{
		if(user_agent.IndexOf("Android") >= 0)
		{
			browser.os_type = RtcOsType::Android;
		}
		else if(user_agent.IndexOf("iPhone") >= 0)
		{
			browser.os_type = RtcOsType::iOS;
		}
		else if(user_agent.IndexOf("Windows") >= 0)
		{
			// Windows Phone
		}
		else
		{
			// Other
		}
	}
	else if(user_agent.IndexOf("Windows") >= 0)
	{
		browser.os_type = RtcOsType::Windows;
	}
	else if(user_agent.IndexOf("Macintosh") >= 0)
	{
		browser.os_type = RtcOsType::macOS;
	}
	else if(user_agent.IndexOf("Linux") >= 0)
	{
		browser.os_type = RtcOsType::Linux;
	}
	else
	{
		// Other
	}

	// Detect browser type
	if(user_agent.IndexOf("Chrome") >= 0)
	{
		browser.browser_type = RtcBrowserType::Chrome;
	}
	else if(user_agent.IndexOf("Safari") >= 0)
	{
		browser.browser_type = RtcBrowserType::Safari;
	}
	else if((user_agent.IndexOf("Firefox") >= 0) && (user_agent.IndexOf("SeaMonkey") < 0))
	{
		browser.browser_type = RtcBrowserType::Firefox;
	}
	else if(user_agent.IndexOf("Edge") >= 0)
	{
		browser.browser_type = RtcBrowserType::Edge;
	}
#if 0
		else if(user_agent.IndexOf("Chromium") >= 0)
		{
		}
		else if((user_agent.IndexOf("OPR") >= 0) || (user_agent.IndexOf("Opera") >= 0))
		{
		}
		else if((user_agent.IndexOf("MSIE") >= 0))
		{
		}
#endif
	else
	{
		// Other
	}

	return browser;
}

bool RtcPeerInfo::IsCompatibleWith(const std::shared_ptr<RtcPeerInfo> &peer)
{
	if(_browser.browser_type == RtcBrowserType::Other)
	{
		// Unknown browser cannot become host peer
		return false;
	}

	return (_browser.browser_type == peer->_browser.browser_type);
}

ov::String RtcPeerInfo::ToString() const
{
	if(IsHost())
	{
		return ov::String::FormatString(
			"<PeerInfo: %p, Host peer, id: %d, can accept: %s, client count: %zu, browser: %s>",
			this,
			_id,
			_can_accept ? "true" : "false",
			_client_list.size(),
			_browser.ToString().CStr()
		);
	}
	else
	{
		return ov::String::FormatString(
			"<PeerInfo: %p, Client peer, id: %d, can accept: %s, browser: %s>",
			this,
			_id,
			_can_accept ? "true" : "false",
			_browser.ToString().CStr()
		);
	}
}
