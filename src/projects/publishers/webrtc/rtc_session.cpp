//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtc_session.h"

#include <utility>

#include "base/info/stream.h"
#include "modules/rtp_rtcp/rtcp_info/nack.h"
#include "modules/rtp_rtcp/rtcp_info/remb.h"
#include "modules/rtp_rtcp/rtcp_info/transport_cc.h"
#include "rtc_application.h"
#include "rtc_common_types.h"
#include "rtc_private.h"
#include "rtc_stream.h"
#include "webrtc_publisher.h"

std::shared_ptr<RtcSession> RtcSession::Create(const std::shared_ptr<WebRtcPublisher> &publisher,
											   const std::shared_ptr<pub::Application> &application,
											   const std::shared_ptr<pub::Stream> &stream,
											   const ov::String &file_name,
											   const std::shared_ptr<const SessionDescription> &offer_sdp,
											   const std::shared_ptr<const SessionDescription> &peer_sdp,
											   const std::shared_ptr<IcePort> &ice_port,
											   session_id_t ice_session_id,
											   const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session)
{
	// Session Id of the offer sdp is unique value
	auto session_info = info::Session(*std::static_pointer_cast<info::Stream>(stream), offer_sdp->GetSessionId());
	auto session	  = std::make_shared<RtcSession>(session_info, publisher, application, stream, file_name, offer_sdp, peer_sdp, ice_port, ice_session_id, ws_session);
	if (!session->Start())
	{
		return nullptr;
	}
	return session;
}

RtcSession::RtcSession(const info::Session &session_info,
					   const std::shared_ptr<WebRtcPublisher> &publisher,
					   const std::shared_ptr<pub::Application> &application,
					   const std::shared_ptr<pub::Stream> &stream,
					   const ov::String &file_name,
					   const std::shared_ptr<const SessionDescription> &offer_sdp,
					   const std::shared_ptr<const SessionDescription> &peer_sdp,
					   const std::shared_ptr<IcePort> &ice_port,
					   session_id_t ice_session_id,
					   const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session)
	: Session(session_info, application, stream), Node(NodeType::Edge)
{
	_publisher		= publisher;
	_offer_sdp		= offer_sdp;
	_peer_sdp		= peer_sdp;
	_ice_port		= ice_port;
	_ice_session_id = ice_session_id;
	_ws_session		= ws_session;
	_file_name		= file_name;
}

RtcSession::~RtcSession()
{
	Stop();
	logtt("RtcSession(%d) has been terminated finally", GetId());
}

bool RtcSession::Start()
{
	// start and stop must be called independently.
	std::lock_guard<std::shared_mutex> lock(_start_stop_lock);

	if (pub::Session::GetState() != SessionState::Ready)
	{
		return false;
	}

	logtt("[WebRTC Publisher] OfferSDP");
	logtt("%s\n", _offer_sdp->ToString().CStr());
	logtt("[WebRTC Publisher] AnswerSDP");
	logtt("%s", _peer_sdp->ToString().CStr());

	auto offer_media_desc_list = _offer_sdp->GetMediaList();
	auto peer_media_desc_list  = _peer_sdp->GetMediaList();

	if (offer_media_desc_list.size() != peer_media_desc_list.size())
	{
		logte("m= line of answer does not correspond with offer");
		return false;
	}

	// Create nodes

	_rtp_rtcp									= std::make_shared<RtpRtcp>(RtpRtcpInterface::GetSharedPtr());
	_srtp_transport								= std::make_shared<SrtpTransport>();

	_dtls_transport								= std::make_shared<DtlsTransport>();
	std::shared_ptr<RtcApplication> application = std::static_pointer_cast<RtcApplication>(GetApplication());
	_dtls_transport->SetLocalCertificate(application->GetCertificate());
	_dtls_transport->StartDTLS();

	// RFC3264
	// For each "m=" line in the offer, there MUST be a corresponding "m=" line in the answer.
	for (size_t i = 0; i < peer_media_desc_list.size(); i++)
	{
		auto peer_media_desc  = peer_media_desc_list[i];
		auto offer_media_desc = offer_media_desc_list[i];

		// The first payload has the highest priority.
		auto first_payload	  = peer_media_desc->GetFirstPayload();
		if (first_payload == nullptr)
		{
			logte("Failed to get the first Payload type of peer sdp");
			return false;
		}

		if (peer_media_desc->GetMediaType() == MediaDescription::MediaType::Audio)
		{
			_audio_payload_type = first_payload->GetId();
			_audio_ssrc			= offer_media_desc->GetSsrc().value_or(0);
			_rtp_rtcp->AddRtpSender(_audio_payload_type, _audio_ssrc, first_payload->GetCodecRate(), offer_media_desc->GetCname().value_or(""));
		}
		else
		{
			_video_payload_type = first_payload->GetId();
			_video_ssrc			= offer_media_desc->GetSsrc().value_or(0);
			_rtp_rtcp->AddRtpSender(_video_payload_type, _video_ssrc, first_payload->GetCodecRate(), offer_media_desc->GetCname().value_or(""));

			auto payload = peer_media_desc->GetPayload(static_cast<uint8_t>(FixedRtcPayloadType::RED_PAYLOAD_TYPE));
			if (payload != nullptr)
			{
				if (payload->GetCodec() == PayloadAttr::SupportCodec::RED)
				{
					_red_enabled = true;
				}
			}

			// Retransmission, We always define the RTX payload as payload + 1
			payload = peer_media_desc->GetPayload(_video_payload_type + 1);
			if (payload != nullptr)
			{
				if (payload->GetCodec() == PayloadAttr::SupportCodec::RTX)
				{
					_rtx_enabled = true;
				}
			}
		}
	}

	// Get Playlist
	_playlist = std::static_pointer_cast<RtcStream>(GetStream())->GetRtcPlaylist(_file_name, CodecIdFromPayloadTypeNumber(_video_payload_type), CodecIdFromPayloadTypeNumber(_audio_payload_type));
	if (_playlist == nullptr)
	{
		logte("Failed to get the playlist (%s/%s/%s) because there is no available rendition", GetApplication()->GetVHostAppName().CStr(), GetStream()->GetName().CStr(), _file_name.CStr());
		return false;
	}

	_auto_abr		   = _playlist->IsWebRtcAutoAbr();

	_current_rendition = _playlist->GetFirstRendition();
	RecordAutoSelectedRendition(_current_rendition, true);

	auto current_video_track = _current_rendition->GetVideoTrack();
	auto current_audio_track = _current_rendition->GetAudioTrack();

	SendPlaylistInfo(_playlist);
	SendRenditionChanged(_current_rendition);

	logtt("Video PT(%d) Audio PT(%d) Video TrackID(%u) Audio TrackID(%u)", _video_payload_type, _audio_payload_type,
		  current_video_track ? current_video_track->GetId() : -1,
		  current_audio_track ? current_audio_track->GetId() : -1);

	// Connect nodes

	// [RTP_RTCP] -> [SRTP] -> [DTLS] -> [RtcSession(Edge)]

	_rtp_rtcp->RegisterPrevNode(nullptr);
	_rtp_rtcp->RegisterNextNode(_srtp_transport);
	_rtp_rtcp->Start();
	_srtp_transport->RegisterPrevNode(_rtp_rtcp);
	_srtp_transport->RegisterNextNode(_dtls_transport);
	_srtp_transport->Start();
	_dtls_transport->RegisterPrevNode(_srtp_transport);
	_dtls_transport->RegisterNextNode(ov::Node::GetSharedPtr());
	_dtls_transport->Start();

	RegisterPrevNode(_dtls_transport);
	RegisterNextNode(nullptr);
	ov::Node::Start();

	_abr_test_watch.Start();
	_bitrate_estimate_watch.Start();

	return Session::Start();
}

bool RtcSession::Stop()
{
	// start and stop must be called independently.
	std::lock_guard<std::shared_mutex> lock(_start_stop_lock);

	logtt("Stop session. Peer sdp session id : %u", GetOfferSDP()->GetSessionId());

	if (pub::Session::GetState() != SessionState::Started && pub::Session::GetState() != SessionState::Stopping)
	{
		return true;
	}

	if (_rtp_rtcp != nullptr)
	{
		_rtp_rtcp->Stop();
	}

	if (_dtls_transport != nullptr)
	{
		_dtls_transport->Stop();
	}

	if (_srtp_transport != nullptr)
	{
		_srtp_transport->Stop();
	}

	// TODO(Getroot): Doesn't need this?
	//_ws_session->Close();

	ov::Node::Stop();

	return Session::Stop();
}

void RtcSession::SetSessionExpiredTime(uint64_t expired_time)
{
	_session_expired_time = expired_time;
}

const std::shared_ptr<const SessionDescription> &RtcSession::GetOfferSDP() const
{
	return _offer_sdp;
}

const std::shared_ptr<const SessionDescription> &RtcSession::GetPeerSDP() const
{
	return _peer_sdp;
}

const std::shared_ptr<http::svr::ws::WebSocketSession> &RtcSession::GetWSClient()
{
	return _ws_session;
}

bool RtcSession::RequestChangeRendition(const ov::String &rendition_name)
{
	auto rendition = _playlist->GetRendition(rendition_name);
	if (rendition == nullptr)
	{
		logte("Failed to get the rendition (%s) because there is no available rendition", rendition_name.CStr());
		return false;
	}

	std::lock_guard<std::shared_mutex> lock(_change_rendition_lock);

	if (rendition == _current_rendition)
	{
		logtt("The rendition (%s) is already selected", rendition_name.CStr());
		return true;
	}

	_next_rendition = rendition;

	return true;
}

bool RtcSession::RequestChangeRendition(SwitchOver switch_over)
{
	std::lock_guard<std::shared_mutex> lock(_change_rendition_lock);

	// Changing
	if (_next_rendition != nullptr)
	{
		return false;
	}

	std::shared_ptr<const RtcRendition> next_rendition = nullptr;

	if (switch_over == SwitchOver::HIGHER)
	{
		next_rendition = _playlist->GetNextHigherBitrateRendition(_current_rendition);
	}
	else
	{
		next_rendition = _playlist->GetNextLowerBitrateRendition(_current_rendition);
	}

	if (next_rendition != nullptr)
	{
		logtt("Change rendition - EstimatedBandwidth(%f) CurrentRendition(%s - %" PRIu64 ") NextRendition(%s - %" PRIu64 ")",
			  _estimated_bitrates, _current_rendition->GetName().CStr(), _current_rendition->GetBitrates(), next_rendition->GetName().CStr(), next_rendition->GetBitrates());

		_next_rendition = next_rendition;
	}

	return true;
}

bool RtcSession::SetAutoAbr(bool auto_abr)
{
	_auto_abr = auto_abr;
	SendRenditionChanged(_current_rendition);
	return true;
}

bool RtcSession::SendPlaylistInfo(const std::shared_ptr<const RtcPlaylist> &playlist) const
{
	auto ws_response = std::static_pointer_cast<http::svr::ws::WebSocketResponse>(_ws_session->GetResponse());
	if (ws_response == nullptr)
	{
		logte("Failed to get the websocket response");
		return false;
	}

	Json::Value json_response;

	json_response["command"] = "notification";
	json_response["type"]	 = "playlist";

	// Message
	json_response["message"] = playlist->ToJson(_auto_abr);

	return ws_response->Send(json_response) > 0;
}

bool RtcSession::SendRenditionChanged(const std::shared_ptr<const RtcRendition> &rendition) const
{
	auto ws_response = std::static_pointer_cast<http::svr::ws::WebSocketResponse>(_ws_session->GetResponse());
	if (ws_response == nullptr)
	{
		logte("Failed to get the websocket response");
		return false;
	}

	Json::Value json_response;

	json_response["command"] = "notification";
	json_response["type"]	 = "rendition_changed";

	// Message
	Json::Value json_message;

	json_message["rendition_name"] = rendition->GetName().CStr();
	json_message["auto"]		   = _auto_abr;

	json_response["message"]	   = json_message;

	return ws_response->Send(json_response) > 0;
}

void RtcSession::OnMessageReceived(const std::any &message)
{
	//It must not be called during start and stop.
	std::shared_lock<std::shared_mutex> lock(_start_stop_lock);

	std::shared_ptr<const ov::Data> data = nullptr;
	try
	{
		data = std::any_cast<std::shared_ptr<const ov::Data>>(message);
		if (data == nullptr)
		{
			return;
		}
	}
	catch (const std::bad_any_cast &e)
	{
		logtt("An incorrect type of packet was input from the stream.");
		return;
	}

	_received_bytes += data->GetLength();

	logtt("Received %" PRIu64 " bytes", _received_bytes);

	// RTP_RTCP -> SRTP -> DTLS -> Edge Node(RtcSession)
	SendDataToPrevNode(data);
}

void RtcSession::ChangeRendition()
{
	std::unique_lock<std::shared_mutex> lock(_change_rendition_lock);

	if (_next_rendition == nullptr)
	{
		return;
	}

	if (_next_rendition == _current_rendition)
	{
		logtt("The rendition is already selected");
		return;
	}

	_current_rendition = _next_rendition;
	_next_rendition	   = nullptr;

	lock.unlock();

	SendRenditionChanged(_current_rendition);
}

uint8_t RtcSession::GetOriginPayloadTypeFromRedRtpPacket(const std::shared_ptr<const RedRtpPacket> &red_rtp_packet)
{
	uint8_t rtp_payload_type = 0;

	// RED includes FEC packet or Media packet.
	if (red_rtp_packet->IsUlpfec())
	{
		rtp_payload_type = red_rtp_packet->OriginPayloadType();
	}
	else
	{
		rtp_payload_type = red_rtp_packet->BlockPT();
	}

	return rtp_payload_type;
}

bool RtcSession::IsSelectedPacket(const std::shared_ptr<const RtpPacket> &rtp_packet)
{
	std::shared_lock<std::shared_mutex> change_lock(_change_rendition_lock);
	auto next_rendition = _next_rendition;
	change_lock.unlock();

	// Check if need to change rendition
	if (next_rendition != nullptr)
	{
		// Try to change rendition
		// When a video packet of the next rendition is a keyframe
		if (next_rendition->GetVideoTrack() != nullptr && next_rendition->GetVideoTrack()->GetId() == rtp_packet->GetTrackId() && rtp_packet->IsKeyframe())
		{
			ChangeRendition();
		}
		// Can change rendition immediately if next rendition has only audio track
		else if (next_rendition->GetVideoTrack() == nullptr)
		{
			ChangeRendition();
		}
	}

	change_lock.lock();
	auto current_rendition = _current_rendition;
	change_lock.unlock();

	// Select Track for ABR
	auto selected_track = rtp_packet->IsVideoPacket() ? current_rendition->GetVideoTrack() : current_rendition->GetAudioTrack();
	if (selected_track == nullptr || rtp_packet->GetTrackId() != selected_track->GetId())
	{
		return false;
	}

	uint32_t rtp_payload_type = rtp_packet->PayloadType();

	if (rtp_payload_type == _audio_payload_type ||
		// if RED is disabled, origin RTP packet is selected
		(_red_enabled == false && rtp_payload_type == _video_payload_type) ||
		// if RED is enabled, RED packet is selected
		(_red_enabled == true && rtp_payload_type == static_cast<uint8_t>(FixedRtcPayloadType::RED_PAYLOAD_TYPE)))
	{
		return true;
	}

	return false;
}

void RtcSession::SendOutgoingData(const std::any &packet)
{
	// ABR Test Codes
	// if (_changed == false && _abr_test_watch.IsElapsed(5000))
	// {
	// 	RequestChangeRendition("480p");
	// }

	// if (_changed == false && _abr_test_watch.IsElapsed(10000))
	// {
	// 	RequestChangeRendition("720p");
	// 	_changed = true;
	// }

	//It must not be called during start and stop.
	std::shared_lock<std::shared_mutex> lock(_start_stop_lock);

	if (pub::Session::GetState() != SessionState::Started)
	{
		return;
	}

	// Check expired time
	if (_session_expired_time != 0 && _session_expired_time < ov::Clock::NowMSec())
	{
		_ice_port->DisconnectSession(_ice_session_id);
		SetState(SessionState::Stopping);
		return;
	}

	std::shared_ptr<RtpPacket> session_packet;

	try
	{
		session_packet = std::any_cast<std::shared_ptr<RtpPacket>>(packet);
		if (session_packet == nullptr)
		{
			return;
		}
	}
	catch (const std::bad_any_cast &e)
	{
		logtt("An incorrect type of packet was input from the stream.");
		return;
	}

	// Check the packet is selected.
	if (IsSelectedPacket(session_packet) == false)
	{
		return;
	}

	// RTP Session must be copied and sent because data is altered due to SRTP.
	auto copy_packet = std::make_shared<RtpPacket>(*session_packet);

	if (copy_packet->IsVideoPacket())
	{
		copy_packet->SetSequenceNumber(_video_rtp_sequence_number++);
	}
	else
	{
		copy_packet->SetSequenceNumber(_audio_rtp_sequence_number++);
	}

	// Set transport-wide sequence number
	SetTransportWideSequenceNumber(copy_packet, _wide_sequence_number);
	SetAbsSendTime(copy_packet, ov::Clock::NowMSec());

	// rtp_rtcp -> srtp -> dtls -> Edge Node(RtcSession)

	// Packet loss simulation codes
	// if (ov::Random::GenerateUInt32(1, 33) != 10)
	{
		_rtp_rtcp->SendRtpPacket(copy_packet);
	}

	RecordRtpSent(copy_packet, session_packet->SequenceNumber(), _wide_sequence_number);

	_wide_sequence_number++;

	MonitorInstance->IncreaseBytesOut(*GetStream(), PublisherType::Webrtc, copy_packet->GetDataLength());
}

bool RtcSession::SetTransportWideSequenceNumber(const std::shared_ptr<RtpPacket> &rtp_packet, uint16_t wide_sequence_number)
{
	auto extension_buffer = rtp_packet->Extension(RTP_HEADER_EXTENSION_TRANSPORT_CC_ID);
	if (extension_buffer == nullptr)
	{
		return false;
	}

	auto payload_offset = rtp_packet->GetExtensionType() == RtpHeaderExtension::HeaderType::ONE_BYTE_HEADER ? 1 : 2;

	ByteWriter<uint16_t>::WriteBigEndian(extension_buffer + payload_offset, wide_sequence_number);

	return true;
}

bool RtcSession::SetAbsSendTime(const std::shared_ptr<RtpPacket> &rtp_packet, uint64_t time_ms)
{
	auto extension_buffer = rtp_packet->Extension(RTP_HEADER_EXTENSION_ABS_SEND_TIME_ID);
	if (extension_buffer == nullptr)
	{
		return false;
	}

	auto payload_offset = rtp_packet->GetExtensionType() == RtpHeaderExtension::HeaderType::ONE_BYTE_HEADER ? 1 : 2;

	auto abs_send_time	= RtpHeaderExtensionAbsSendTime::MsToAbsSendTime(time_ms);
	ByteWriter<uint24_t>::WriteBigEndian(extension_buffer + payload_offset, abs_send_time);

	return true;
}

bool RtcSession::RecordRtpSent(const std::shared_ptr<const RtpPacket> &rtp_packet, uint16_t origin_sequence_number, uint16_t wide_sequence_number)
{
	if (rtp_packet == nullptr)
	{
		return false;
	}

	auto sent_log					  = std::make_shared<RtpSentLog>();
	sent_log->_sequence_number		  = rtp_packet->SequenceNumber();
	sent_log->_wide_sequence_number	  = wide_sequence_number;
	sent_log->_track_id				  = rtp_packet->GetTrackId();
	sent_log->_payload_type			  = rtp_packet->PayloadType();
	sent_log->_origin_sequence_number = origin_sequence_number;
	sent_log->_timestamp			  = rtp_packet->Timestamp();
	sent_log->_marker				  = rtp_packet->Marker();
	sent_log->_ssrc					  = rtp_packet->Ssrc();

	sent_log->_sent_bytes			  = rtp_packet->GetDataLength();
	sent_log->_sent_time			  = std::chrono::system_clock::now();

	auto video_rtp_key				  = sent_log->_sequence_number % MAX_RTP_RECORDS;
	auto wide_rtp_key				  = sent_log->_wide_sequence_number % MAX_RTP_RECORDS;

	std::lock_guard<std::shared_mutex> lock(_rtp_record_map_lock);

	if (rtp_packet->IsVideoPacket())
	{
		_video_rtp_sent_record_map[video_rtp_key] = sent_log;
	}
	_wide_rtp_sent_record_map[wide_rtp_key] = sent_log;

	return true;
}

std::shared_ptr<RtcSession::RtpSentLog> RtcSession::TraceRtpSentByVideoSeqNo(uint16_t sequence_number)
{
	std::shared_lock<std::shared_mutex> lock(_rtp_record_map_lock);

	auto key = sequence_number % MAX_RTP_RECORDS;
	auto it	 = _video_rtp_sent_record_map.find(key);
	if (it == _video_rtp_sent_record_map.end())
	{
		return nullptr;
	}

	return it->second;
}

// Get RTP Sent Log from RTP History
std::shared_ptr<RtcSession::RtpSentLog> RtcSession::TraceRtpSentByWideSeqNo(uint16_t wide_sequence_number)
{
	std::shared_lock<std::shared_mutex> lock(_rtp_record_map_lock);

	auto key = wide_sequence_number % MAX_RTP_RECORDS;
	auto it	 = _wide_rtp_sent_record_map.find(key);
	if (it == _wide_rtp_sent_record_map.end())
	{
		return nullptr;
	}

	return it->second;
}

void RtcSession::OnRtpFrameReceived(const std::vector<std::shared_ptr<RtpPacket>> &rtp_packets)
{
	// No player sends RTP packet
}

void RtcSession::OnRtcpReceived(const std::shared_ptr<RtcpInfo> &rtcp_info)
{
	if (pub::Session::GetState() != SessionState::Started)
	{
		return;
	}

	if (rtcp_info->GetPacketType() == RtcpPacketType::RR)
	{
		// Process
		ProcessReceiverReport(rtcp_info);
	}
	else if (rtcp_info->GetPacketType() == RtcpPacketType::RTPFB)
	{
		if (rtcp_info->GetCountOrFmt() == static_cast<uint8_t>(RTPFBFMT::NACK))
		{
			// Process
			ProcessNACK(rtcp_info);
		}
		else if (rtcp_info->GetCountOrFmt() == static_cast<uint8_t>(RTPFBFMT::TRANSPORT_CC))
		{
			// Process
			ProcessTransportCc(rtcp_info);
		}
	}
	else if (rtcp_info->GetPacketType() == RtcpPacketType::PSFB)
	{
		if (rtcp_info->GetCountOrFmt() == static_cast<uint8_t>(PSFBFMT::AFB))  // REMB
		{
			// Process
			ProcessRemb(rtcp_info);
		}
	}

	//rtcp_info->DebugPrint();
}

bool RtcSession::ProcessReceiverReport(const std::shared_ptr<RtcpInfo> &rtcp_info)
{
	auto rr = std::static_pointer_cast<ReceiverReport>(rtcp_info);
	if (rr == nullptr)
	{
		logte("Could not parse ReceiverReport");
		return false;
	}

	//rr->DebugPrint();

	return true;
}

bool RtcSession::ProcessNACK(const std::shared_ptr<RtcpInfo> &rtcp_info)
{
	if (_rtx_enabled == false)
	{
		return true;
	}

	auto stream = std::static_pointer_cast<RtcStream>(GetStream());
	if (stream == nullptr)
	{
		return false;
	}

	auto nack = std::static_pointer_cast<NACK>(rtcp_info);
	if (nack->GetMediaSsrc() != _video_ssrc)
	{
		return false;
	}

	// Retransmission
	for (size_t i = 0; i < nack->GetLostIdCount(); i++)
	{
		auto seq_no	  = nack->GetLostId(i);
		auto sent_log = TraceRtpSentByVideoSeqNo(seq_no);
		if (sent_log == nullptr)
		{
			continue;
		}

		logtt("RTX requested(%d) - TrackID(%u) PayloadType(%d) OriginSeqNo(%u)", seq_no, sent_log->_track_id, sent_log->_payload_type, sent_log->_origin_sequence_number);

		auto rtx_packet = stream->GetRtxRtpPacket(sent_log->_track_id, sent_log->_payload_type, sent_log->_origin_sequence_number);
		if (rtx_packet != nullptr)
		{
			auto copy_rtx_packet = std::make_shared<RtxRtpPacket>(*rtx_packet);
			copy_rtx_packet->SetSequenceNumber(_rtx_sequence_number++);
			copy_rtx_packet->SetOriginalSequenceNumber(sent_log->_sequence_number);
			return _rtp_rtcp->SendRtpPacket(copy_rtx_packet);
		}
	}

	return true;
}

bool RtcSession::ProcessTransportCc(const std::shared_ptr<RtcpInfo> &rtcp_info)
{
	auto transport_cc = std::static_pointer_cast<TransportCc>(rtcp_info);

	if (transport_cc->GetPacketStatusCount() == 0)
	{
		// what?
		return false;
	}

	uint64_t sent_bytes	  = 0;
	int64_t sent_duration = 0;

	// For debug
	// logtt("TransportCC - SenderSsrc(%u) MediaSsrc(%u) PacketStatusCount(%u)", transport_cc->GetSenderSsrc(), transport_cc->GetMediaSsrc(), transport_cc->GetPacketStatusCount());

	// for (size_t i = 0; i < transport_cc->GetPacketStatusCount(); i++)
	// {
	// 	auto packet_status = transport_cc->GetPacketFeedbackInfo(i);
	// 	auto sent_log = TraceRtpSentByWideSeqNo(packet_status->_wide_sequence_number);
	// 	if (sent_log == nullptr)
	// 	{
	// 		logte("TransportCC - No sent log found for seqno(%u)", packet_status->_wide_sequence_number);
	// 		continue;
	// 	}

	// 	logtt("%s", sent_log->ToString().CStr());
	// }

	// return true;

	for (size_t i = 1; i < transport_cc->GetPacketStatusCount(); i++)
	{
		auto packet_status = transport_cc->GetPacketFeedbackInfo(i);
		auto sent_log	   = TraceRtpSentByWideSeqNo(packet_status->_wide_sequence_number);
		if (sent_log == nullptr)
		{
			logte("TransportCC - No sent log found for seqno(%u)", packet_status->_wide_sequence_number);
			continue;
		}
		auto prev_sent_log = TraceRtpSentByWideSeqNo(packet_status->_wide_sequence_number - 1);
		if (prev_sent_log == nullptr)
		{
			logtt("TransportCC - No prev sent log found for seqno(%u)", packet_status->_wide_sequence_number - 1);
			continue;
		}

		// Calc delta of sent_log and prev_sent_log
		int32_t sent_delta_time = (std::chrono::duration_cast<std::chrono::microseconds>(sent_log->_sent_time - prev_sent_log->_sent_time).count()) / 250;

		auto duration			= packet_status->_received_delta - sent_delta_time;
		if (duration <= 0)
		{
			//duration = 1;
		}

		sent_bytes += sent_log->_sent_bytes;
		sent_duration += duration;

		logtt("WideSeqNo(%u) Refer(%u) RecvDelta(%u) SentDelta(%u) SentBytes(%u) Duration(%d)", packet_status->_wide_sequence_number, transport_cc->GetReferenceTime(), packet_status->_received_delta, sent_delta_time, sent_log->_sent_bytes, duration);
	}

	if (sent_bytes > 0)
	{
		double sent_seconds = static_cast<double>(sent_duration) / 4000.0;

		_total_sent_seconds += sent_seconds;
		_total_sent_bytes += sent_bytes;
		_estimated_bitrates = static_cast<double>((_total_sent_bytes * 8)) / _total_sent_seconds;

		if (_bitrate_estimate_watch.IsElapsed(1000) == true)
		{
			_bitrate_estimate_watch.Update();
			ChangeRenditionIfNeeded();

			logtt("Estimated Bandwidth(%f) TotalSentBytes(%" PRIu64 ") TotalSentSeconds(%f)", static_cast<double>(_total_sent_bytes * 8) / _total_sent_seconds, _total_sent_bytes, _total_sent_seconds);

			_total_sent_seconds = 0;
			_total_sent_bytes	= 0;
		}
	}

	return true;
}

bool RtcSession::ProcessRemb(const std::shared_ptr<RtcpInfo> &rtcp_info)
{
	auto remb = std::static_pointer_cast<REMB>(rtcp_info);

	logtt("REMB Estimated Bandwidth(%" PRId64 ")", remb->GetBitrateBps());

	_previous_estimated_bitrate = _estimated_bitrates;
	_estimated_bitrates			= remb->GetBitrateBps();

	if (_bitrate_estimate_watch.IsElapsed(1000) == true)
	{
		_bitrate_estimate_watch.Update();
		ChangeRenditionIfNeeded();
	}

	return true;
}

void RtcSession::ChangeRenditionIfNeeded()
{
	if (_auto_abr == false)
	{
		return;
	}

	auto current_rendition_bitrates = _current_rendition->GetBitrates();

	// If the estimated bitrates are not high enough (10%) then the playback may not be smooth, so go for the lower bitrates.
	if (1.1 * _estimated_bitrates <= current_rendition_bitrates)
	{
		auto lower = _playlist->GetNextLowerBitrateRendition(_current_rendition);
		if (lower != nullptr && IsNextRenditionGoodChoice(lower) == true)
		{
			logtt("ChangeRenditionIfNeeded - Change to low bitrate");
			if (RequestChangeRendition(SwitchOver::LOWER) == true)
			{
				RecordAutoSelectedRendition(lower, false);
			}
		}
	}
	else if (0.75 * _estimated_bitrates > current_rendition_bitrates)
	{
		// Next higher rendition
		auto upper = _playlist->GetNextHigherBitrateRendition(_current_rendition);
		if (upper != nullptr && IsNextRenditionGoodChoice(upper) == true)
		{
			logtt("ChangeRenditionIfNeeded - Change to high bitrate");
			if (RequestChangeRendition(SwitchOver::HIGHER) == true)
			{
				RecordAutoSelectedRendition(upper, true);
			}
		}
	}
}

bool RtcSession::RecordAutoSelectedRendition(const std::shared_ptr<const RtcRendition> &rendition, bool higher_quality)
{
	if (rendition == nullptr)
	{
		return false;
	}

	std::shared_ptr<SelectedRecord> record;
	auto it = _auto_rendition_selected_records.find(rendition->GetName());
	if (it == _auto_rendition_selected_records.end())
	{
		record												   = std::make_shared<SelectedRecord>();
		record->_rendition_name								   = rendition->GetName();
		_auto_rendition_selected_records[rendition->GetName()] = record;
	}
	else
	{
		record = it->second;
	}

	record->_selected_count++;
	record->_last_selected_time = std::chrono::system_clock::now();

	if (higher_quality == true)
	{
		_switched_rendition_to_higher	   = true;
		_switched_rendition_to_higher_time = std::chrono::system_clock::now();
	}
	else
	{
		_switched_rendition_to_higher = false;
	}

	return true;
}

bool RtcSession::IsNextRenditionGoodChoice(const std::shared_ptr<const RtcRendition> &rendition)
{
	auto current_bitrates = _current_rendition->GetBitrates();
	auto next_bitrates	  = rendition->GetBitrates();

	// Go lower
	if (current_bitrates > next_bitrates)
	{
		if (_previous_estimated_bitrate < _estimated_bitrates)
		{
			// The bandwidth is getting higher, so let's wait
			return false;
		}

		if (_switched_rendition_to_higher == true)
		{
			auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - _switched_rendition_to_higher_time).count();

			// We have switched to a higher rendition recently, so let's wait
			if (elapsed_time < 10)
			{
				return false;
			}
		}

		// Since the next rendition has lower bitrates, it will run better than it is now.
		return true;
	}
	// Go higher
	else
	{
		auto it = _auto_rendition_selected_records.find(rendition->GetName());
		if (it == _auto_rendition_selected_records.end())
		{
			// Never experienced it, let's try.
			return true;
		}

		auto record		  = it->second;

		// get elapsed time from now
		auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - record->_last_selected_time).count();

		if (elapsed_time > 10 * (/*1 <<*/ record->_selected_count))
		{
			// 10 -> 20 -> 30 -> 40 seconds after it can be selected again
			// It has been long enough since the last time we selected this rendition.
			return true;
		}
	}

	return false;
}

// ov::Node Interface
// RtpRtcp -> SRTP -> DTLS -> Edge(this)
bool RtcSession::OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data)
{
	if (ov::Node::GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

	return _ice_port->Send(_ice_session_id, data);
}

// RtcSession Node has not a lower node so it will not be called
bool RtcSession::OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data)
{
	return true;
}