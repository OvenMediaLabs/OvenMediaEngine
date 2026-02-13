//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "webrtc_publisher.h"

#include <orchestrator/orchestrator.h>

#include <utility>

#include "config/config_manager.h"
#include "rtc_private.h"
#include "rtc_session.h"
#include "rtc_stream.h"
#include "webrtc_publisher_signalling_interceptor.h"

std::shared_ptr<WebRtcPublisher> WebRtcPublisher::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
{
	auto webrtc = std::make_shared<WebRtcPublisher>(server_config, router);

	if (!webrtc->Start())
	{
		return nullptr;
	}
	return webrtc;
}

WebRtcPublisher::WebRtcPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	: Publisher(server_config, router)
{
}

WebRtcPublisher::~WebRtcPublisher()
{
	logtt("WebRtcPublisher has been terminated finally");
}

bool WebRtcPublisher::StartSignallingServer(const cfg::Server &server_config, const cfg::bind::cmm::Webrtc &webrtc_bind_config)
{
	auto &signalling_config = webrtc_bind_config.GetSignalling();

	bool is_configured;
	auto worker_count = signalling_config.GetWorkerCount(&is_configured);
	worker_count	  = is_configured ? worker_count : HTTP_SERVER_USE_DEFAULT_COUNT;

	bool is_port_configured;
	auto &port_config = signalling_config.GetPort(&is_port_configured);
	bool is_tls_port_configured;
	auto &tls_port_config = signalling_config.GetTlsPort(&is_tls_port_configured);

	if ((is_port_configured == false) && (is_tls_port_configured == false))
	{
		logtw("%s is disabled - No port is configured", GetPublisherName());
		return true;
	}

	auto signalling_server = std::make_shared<RtcSignallingServer>(server_config, webrtc_bind_config);
	signalling_server->AddObserver(RtcSignallingObserver::GetSharedPtr());

	auto interceptor = std::make_shared<WebRtcPublisherSignallingInterceptor>();

	if (signalling_server->Start(
			GetPublisherName(), "RtcSig",
			server_config.GetIPList(),
			is_port_configured, port_config.GetPort(),
			is_tls_port_configured, tls_port_config.GetPort(),
			worker_count, interceptor))
	{
		_signalling_server = std::move(signalling_server);

		return true;
	}

	return false;
}

bool WebRtcPublisher::StartICEPorts(const cfg::Server &server_config, const cfg::bind::cmm::Webrtc &webrtc_bind_config)
{
	auto ice_port = IcePortManager::GetInstance()->CreateTurnServers(
		GetPublisherName(),
		IcePortObserver::GetSharedPtr(),
		server_config, webrtc_bind_config);

	if (ice_port == nullptr)
	{
		return false;
	}

	_ice_port = ice_port;

	return true;
}

bool WebRtcPublisher::Start()
{
	auto server_config		= GetServerConfig();
	auto webrtc_bind_config = server_config.GetBind().GetPublishers().GetWebrtc();

	if (webrtc_bind_config.IsParsed() == false)
	{
		logtw("%s is disabled by configuration", GetPublisherName());
		return true;
	}

	if (StartSignallingServer(server_config, webrtc_bind_config) &&
		StartICEPorts(server_config, webrtc_bind_config))
	{
		return Publisher::Start();
	}

	logte("An error occurred while initialize %s. Stopping RtcSignallingServer...", GetPublisherName());

	if (_signalling_server != nullptr)
	{
		_signalling_server->Stop();
	}

	IcePortManager::GetInstance()->Release(IcePortObserver::GetSharedPtr());

	return false;
}

bool WebRtcPublisher::Stop()
{
	IcePortManager::GetInstance()->Release(IcePortObserver::GetSharedPtr());

	if (_signalling_server != nullptr)
	{
		_signalling_server->RemoveObserver(RtcSignallingObserver::GetSharedPtr());
		_signalling_server->Stop();
	}

	return Publisher::Stop();
}

bool WebRtcPublisher::DisconnectSessionInternal(const std::shared_ptr<RtcSession> &session)
{
	auto stream = std::dynamic_pointer_cast<RtcStream>(session->GetStream());

	stream->RemoveSession(session->GetId());

	MonitorInstance->OnSessionDisconnected(*stream, PublisherType::Webrtc);

	session->Stop();

	return true;
}

bool WebRtcPublisher::OnCreateHost(const info::Host &host_info)
{
	if (_signalling_server != nullptr && host_info.GetCertificate() != nullptr)
	{
		return _signalling_server->InsertCertificate(host_info.GetCertificate());
	}

	return true;
}

bool WebRtcPublisher::OnDeleteHost(const info::Host &host_info)
{
	if (_signalling_server != nullptr && host_info.GetCertificate() != nullptr)
	{
		return _signalling_server->RemoveCertificate(host_info.GetCertificate());
	}
	return true;
}

bool WebRtcPublisher::OnUpdateCertificate(const info::Host &host_info)
{
	if (_signalling_server != nullptr && host_info.GetCertificate() != nullptr)
	{
		return _signalling_server->InsertCertificate(host_info.GetCertificate());
	}

	return true;
}

std::shared_ptr<pub::Application> WebRtcPublisher::OnCreatePublisherApplication(const info::Application &application_info)
{
	if (IsModuleAvailable() == false)
	{
		return nullptr;
	}

	return RtcApplication::Create(pub::Publisher::GetSharedPtrAs<pub::Publisher>(), application_info, _ice_port, _signalling_server);
}

bool WebRtcPublisher::OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application)
{
	return true;
}

/*
 * Signalling Implementation
 */

// Called when receives request offer sdp from client
std::shared_ptr<const SessionDescription> WebRtcPublisher::OnRequestOffer(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
																		  const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
																		  std::vector<RtcIceCandidate> *ice_candidates, bool &tcp_relay)
{
	info::VHostAppName final_vhost_app_name		= vhost_app_name;
	ov::String final_host_name					= host_name;
	ov::String final_stream_name				= stream_name;

	[[maybe_unused]] RequestStreamResult result = RequestStreamResult::init;
	auto request								= ws_session->GetRequest();
	auto remote_address							= request->GetRemote()->GetRemoteAddress();
	auto uri									= request->GetUri();
	auto final_url								= ov::Url::Parse(uri);
	if (final_url == nullptr)
	{
		logte("Could not parse the url: %s", uri.CStr());
		return nullptr;
	}

	ov::String final_file_name = final_url->File();

	// PORT can be omitted if port is default port, but SignedPolicy requires this information.
	if (final_url->Port() == 0)
	{
		final_url->SetPort(request->GetRemote()->GetLocalAddress()->Port());
	}

	auto requested_url						   = final_url;
	auto request_info						   = std::make_shared<ac::RequestInfo>(final_url, nullptr, request->GetRemote(), request);

	uint64_t session_life_time				   = 0;
	auto [signed_policy_result, signed_policy] = Publisher::VerifyBySignedPolicy(request_info);
	if (signed_policy_result == AccessController::VerificationResult::Pass)
	{
		session_life_time = signed_policy->GetStreamExpireEpochMSec();
	}
	else if (signed_policy_result == AccessController::VerificationResult::Error)
	{
		return nullptr;
	}
	else if (signed_policy_result == AccessController::VerificationResult::Fail)
	{
		logtw("%s", signed_policy->GetErrMessage().CStr());
		return nullptr;
	}
	else if (signed_policy_result == AccessController::VerificationResult::Off)
	{
	}

	// Admission Webhooks
	auto [webhooks_result, admission_webhooks] = VerifyByAdmissionWebhooks(request_info);
	if (webhooks_result == AccessController::VerificationResult::Off)
	{
		// Success
	}
	else if (webhooks_result == AccessController::VerificationResult::Pass)
	{
		// Lifetime
		if (admission_webhooks->GetLifetime() != 0)
		{
			// Choice smaller value
			auto stream_expired_msec_from_webhooks = ov::Clock::NowMSec() + admission_webhooks->GetLifetime();
			if (session_life_time == 0 || stream_expired_msec_from_webhooks < session_life_time)
			{
				session_life_time = stream_expired_msec_from_webhooks;
			}
		}

		// Redirect URL
		if (admission_webhooks->GetNewURL() != nullptr)
		{
			final_url = admission_webhooks->GetNewURL();
			if (final_url->Port() == 0)
			{
				final_url->SetPort(request->GetRemote()->GetLocalAddress()->Port());
			}

			final_vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());
			final_host_name		 = final_url->Host();
			final_stream_name	 = final_url->Stream();
			final_file_name		 = final_url->File();
		}
	}
	else if (webhooks_result == AccessController::VerificationResult::Error)
	{
		logtw("AdmissionWebhooks error : %s", final_url->ToUrlString().CStr());
		return nullptr;
	}
	else if (webhooks_result == AccessController::VerificationResult::Fail)
	{
		logtw("AdmissionWebhooks error : %s", admission_webhooks->GetErrReason().CStr());
		return nullptr;
	}

	auto stream = std::static_pointer_cast<RtcStream>(GetStream(final_vhost_app_name, final_stream_name));
	if (stream == nullptr)
	{
		stream = std::dynamic_pointer_cast<RtcStream>(PullStream(final_url, final_vhost_app_name, final_host_name, final_stream_name));
		if (stream == nullptr)
		{
			result = RequestStreamResult::origin_failed;
		}
		else
		{
			logti("URL %s is requested", stream->GetMediaSource().CStr());
		}
	}
	else
	{
		result = RequestStreamResult::local_success;
	}

	if (stream == nullptr)
	{
		logte("Cannot find stream (%s/%s)", final_vhost_app_name.CStr(), final_stream_name.CStr());
		return nullptr;
	}

	// TODO(Getroot): Improve this so that the player's first request is played immediately. This policy was temporarily changed due to a performance issue at the edge.
	if (stream->WaitUntilStart(0) == false)
	{
		logtw("(%s/%s) stream has created but not started.", final_vhost_app_name.CStr(), final_stream_name.CStr());
		return nullptr;
	}

	auto file_sdp = stream->GetSessionDescription(final_file_name);
	if (file_sdp == nullptr)
	{
		logte("Cannot find file (%s/%s/%s)", final_vhost_app_name.CStr(), final_stream_name.CStr(), final_file_name.CStr());
		return nullptr;
	}

	auto transport = final_url->GetQueryValue("transport");
	if (transport.UpperCaseString() == "TCP")
	{
		tcp_relay = true;
	}

	if (_ice_candidate_list.empty() == false)
	{
		auto candidate_index_to_send = _current_ice_candidate_index++ % _ice_candidate_list.size();
		const auto &candidates		 = _ice_candidate_list[candidate_index_to_send];

		ice_candidates->insert(ice_candidates->end(), candidates.cbegin(), candidates.cend());
	}

	// Copy SDP
	auto session_description = std::make_shared<SessionDescription>(*file_sdp);

	session_description->SetOrigin("OvenMediaEngine", ov::Unique::GenerateUint32(), 2, "IN", 4, "127.0.0.1");
	session_description->SetIceUfrag(_ice_port->GenerateUfrag());
	session_description->Update();

	// Passed AccessControl
	ws_session->AddUserData("authorized", true);
	ws_session->AddUserData("final_url", final_url->ToUrlString(true));
	ws_session->AddUserData("requested_url", requested_url->ToUrlString(true));
	ws_session->AddUserData("stream_expired", session_life_time);

	return session_description;
}

// Called when receives an answer sdp from client
bool WebRtcPublisher::OnAddRemoteDescription(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
											 const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
											 const std::shared_ptr<const SessionDescription> &offer_sdp,
											 const std::shared_ptr<const SessionDescription> &answer_sdp)
{
	auto [autorized_exist, authorized] = ws_session->GetUserData("authorized");
	ov::String requested_uri, final_uri;
	uint64_t session_life_time = 0;
	if (autorized_exist == true && std::holds_alternative<bool>(authorized) == true && std::get<bool>(authorized) == true)
	{
		auto [requested_url_exist, requested_url] = ws_session->GetUserData("requested_url");
		if (requested_url_exist == true && std::holds_alternative<ov::String>(requested_url) == true)
		{
			requested_uri = std::get<ov::String>(requested_url);
		}
		else
		{
			return false;
		}

		auto [final_url_exist, final_url] = ws_session->GetUserData("final_url");
		if (final_url_exist == true && std::holds_alternative<ov::String>(final_url) == true)
		{
			final_uri = std::get<ov::String>(final_url);
		}
		else
		{
			return false;
		}

		auto [stream_expired_exist, stream_expired] = ws_session->GetUserData("stream_expired");
		if (stream_expired_exist == true && std::holds_alternative<uint64_t>(stream_expired) == true)
		{
			session_life_time = std::get<uint64_t>(stream_expired);
		}
		else
		{
			return false;
		}
	}
	else
	{
		// This client was unauthoized when request offer situation
		return false;
	}

	auto requested_url = ov::Url::Parse(requested_uri);
	if (requested_url == nullptr)
	{
		logte("Could not parse the url: %s", requested_uri.CStr());
		return false;
	}

	auto final_url = ov::Url::Parse(final_uri);
	if (final_url == nullptr)
	{
		logte("Could not parse the url: %s", final_uri.CStr());
		return false;
	}

	auto final_vhost_app_name  = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());
	auto final_stream_name	   = final_url->Stream();
	auto final_file_name	   = final_url->File();

	auto request			   = ws_session->GetRequest();
	auto remote_address		   = request->GetRemote()->GetRemoteAddress();

	ov::String remote_sdp_text = answer_sdp->ToString();
	logtt("OnAddRemoteDescription: %s", remote_sdp_text.CStr());

	auto application = GetApplicationByName(final_vhost_app_name);
	auto stream		 = std::static_pointer_cast<RtcStream>(GetStream(final_vhost_app_name, final_stream_name));
	if (!stream)
	{
		logte("Cannot find stream (%s/%s)", final_vhost_app_name.CStr(), final_stream_name.CStr());
		return false;
	}

	auto ice_session_id = _ice_port->IssueUniqueSessionId();
	auto session		= RtcSession::Create(Publisher::GetSharedPtrAs<WebRtcPublisher>(), application, stream, final_file_name, offer_sdp, answer_sdp, _ice_port, ice_session_id, ws_session);
	if (session != nullptr)
	{
		session->SetRequestedUrl(requested_url);
		session->SetFinalUrl(final_url);
		if (stream->AddSession(session) == false)
		{
			return false;
		}

		MonitorInstance->OnSessionConnected(*stream, PublisherType::Webrtc);

		auto ice_timeout = application->GetConfig().GetPublishers().GetWebrtcPublisher().GetTimeout();
		_ice_port->AddSession(IcePortObserver::GetSharedPtr(), ice_session_id, IceSession::Role::CONTROLLING, offer_sdp, answer_sdp, ice_timeout, session_life_time, session);
	}
	else
	{
		logte("Cannot create session for (%s/%s/%s)", final_vhost_app_name.CStr(), final_stream_name.CStr(), final_file_name.CStr());
		return false;
	}

	return true;
}

bool WebRtcPublisher::OnChangeRendition(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
										bool change_rendition, const ov::String &rendition_name, bool change_auto, bool &auto_abr,
										const std::shared_ptr<const SessionDescription> &offer_sdp,
										const std::shared_ptr<const SessionDescription> &peer_sdp)
{
	auto [autorized_exist, authorized] = ws_session->GetUserData("authorized");
	ov::String uri;
	if (autorized_exist == true && std::holds_alternative<bool>(authorized) == true && std::get<bool>(authorized) == true)
	{
		auto [final_url_exist, final_url] = ws_session->GetUserData("final_url");
		if (final_url_exist == true && std::holds_alternative<ov::String>(final_url) == true)
		{
			uri = std::get<ov::String>(final_url);
		}
		else
		{
			return false;
		}
	}
	else
	{
		// This client was unauthoized when request offer situation
		return false;
	}

	auto parsed_url = ov::Url::Parse(uri);
	if (parsed_url == nullptr)
	{
		logte("Could not parse the url: %s", uri.CStr());
		return false;
	}

	auto final_vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(parsed_url->Host(), parsed_url->App());
	auto final_stream_name	  = parsed_url->Stream();

	logtt("ChangeRendition command received : %s/%s/%u", final_vhost_app_name.CStr(), final_stream_name.CStr(), offer_sdp->GetSessionId());

	// Find Stream
	auto stream = std::static_pointer_cast<RtcStream>(GetStream(final_vhost_app_name, final_stream_name));
	if (!stream)
	{
		logte("To change rendition in session(%u) failed. Cannot find stream (%s/%s)", offer_sdp->GetSessionId(), final_vhost_app_name.CStr(), final_stream_name.CStr());
		return false;
	}

	auto session = std::static_pointer_cast<RtcSession>(stream->GetSession(offer_sdp->GetSessionId()));
	if (session == nullptr)
	{
		logte("To change rendition in session(%d) failed. Cannot find session by offer sdp session id", offer_sdp->GetSessionId());
		return false;
	}

	if (change_auto == true)
	{
		session->SetAutoAbr(auto_abr);
	}

	if (change_rendition == true)
	{
		session->RequestChangeRendition(rendition_name);
	}

	return true;
}

bool WebRtcPublisher::OnStopCommand(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
									const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
									const std::shared_ptr<const SessionDescription> &offer_sdp,
									const std::shared_ptr<const SessionDescription> &peer_sdp)
{
	logti("Stop command received : %s/%s/%u", vhost_app_name.CStr(), stream_name.CStr(), offer_sdp->GetSessionId());

	info::VHostAppName final_vhost_app_name = vhost_app_name;
	ov::String final_stream_name			= stream_name;

	auto [final_url_exist, url]				= ws_session->GetUserData("final_url");
	if (final_url_exist == true && std::holds_alternative<ov::String>(url) == true)
	{
		ov::String uri					   = std::get<ov::String>(url);

		std::shared_ptr<ov::Url> final_url = ov::Url::Parse(uri);
		if (final_url == nullptr)
		{
			logte("Could not parse the url: %s", uri.CStr());
			return false;
		}

		final_vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());
		final_stream_name	 = final_url->Stream();
	}

	// Find Stream
	auto stream = std::static_pointer_cast<RtcStream>(GetStream(final_vhost_app_name, final_stream_name));
	if (!stream)
	{
		logte("To stop session failed. Cannot find stream (%s/%s)", vhost_app_name.CStr(), stream_name.CStr());
		return false;
	}

	auto session = std::static_pointer_cast<RtcSession>(stream->GetSession(offer_sdp->GetSessionId()));
	if (session == nullptr)
	{
		logte("To stop session failed. Cannot find session by offer sdp session id (%u)", offer_sdp->GetSessionId());
		return false;
	}

	// Send Close to Admission Webhooks
	auto request	   = ws_session->GetRequest();
	auto requested_url = session->GetRequestedUrl();
	auto final_url	   = session->GetFinalUrl();
	auto request_info  = std::make_shared<ac::RequestInfo>(requested_url, final_url, request->GetRemote(), request);
	SendCloseAdmissionWebhooks(request_info);

	DisconnectSessionInternal(session);

	_ice_port->RemoveSession(session->GetIceSessionId());

	return true;
}

bool WebRtcPublisher::OnIceCandidate(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
									 const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
									 const std::shared_ptr<RtcIceCandidate> &candidate,
									 const ov::String &username_fragment)
{
	return true;
}

/*
 * IcePort Implementation
 */

void WebRtcPublisher::OnStateChanged(IcePort &port, uint32_t session_id, IceConnectionState state, std::any user_data)
{
	logtt("IcePort OnStateChanged : %d", ov::ToUnderlyingType(state));

	std::shared_ptr<RtcSession> session;
	try
	{
		session = std::any_cast<std::shared_ptr<RtcSession>>(user_data);
	}
	catch (const std::bad_any_cast &e)
	{
		// Internal Error
		logtc("WebRtcPublisher::OnDataReceived - Could not convert user_data, internal error");
		return;
	}

	auto application = session->GetApplication();
	auto stream		 = std::static_pointer_cast<RtcStream>(session->GetStream());

	switch (state)
	{
		case IceConnectionState::New:
		case IceConnectionState::Checking:
		case IceConnectionState::Connected:
		case IceConnectionState::Completed:
			// Nothing to do
			break;
		case IceConnectionState::Failed:
		case IceConnectionState::Disconnected:
		case IceConnectionState::Closed: {
			logti("IcePort is disconnected. : (%s/%s/%u) reason(%d)", stream->GetApplicationName(), stream->GetName().CStr(), session->GetId(), state);

			_signalling_server->Disconnect(session->GetApplication()->GetVHostAppName(), session->GetStream()->GetName(), session->GetPeerSDP());

			//DisconnectSessionInternal(session);
			break;
		}
		default:
			break;
	}
}

void WebRtcPublisher::OnDataReceived(IcePort &port, uint32_t session_id, std::shared_ptr<const ov::Data> data, std::any user_data)
{
	std::shared_ptr<RtcSession> session;
	try
	{
		session = std::any_cast<std::shared_ptr<RtcSession>>(user_data);
	}
	catch (const std::bad_any_cast &e)
	{
		// Internal Error
		logtc("WebRtcPublisher::OnDataReceived - Could not convert user_data, internal error");
		return;
	}

	logtt("WebRtcPublisher::OnDataReceived : %d", session_id);

	auto stream = session->GetStream();
	stream->SendMessage(session, std::make_any<std::shared_ptr<const ov::Data>>(data));
}
