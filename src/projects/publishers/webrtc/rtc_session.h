//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once
#include <modules/http/server/web_socket/web_socket_session.h>
#include <monitoring/monitoring.h>

#include <unordered_set>

#include "base/info/media_track.h"
#include "base/publisher/session.h"
#include "modules/dtls_srtp/dtls_transport.h"
#include "modules/ice/ice_port.h"
#include "modules/rtp_rtcp/rtp_packetizer_interface.h"
#include "modules/rtp_rtcp/rtp_rtcp.h"
#include "modules/sdp/session_description.h"
#include "rtc_playlist.h"

/*	Node Connection
 * [  RTP_RTCP ]
 * [SRTP] [SCTP]				
 * [    DTLS   ]
 * [  ICE/STUN ]
 * [ RtcSession](Edge Node)	 ----> Send packet
 *		  <----------------------- Recv packet
 *   
 */

class WebRtcPublisher;
class RtcApplication;
class RtcStream;

class RtcSession : public pub::Session, public RtpRtcpInterface, public ov::Node
{
public:
	static std::shared_ptr<RtcSession> Create(const std::shared_ptr<WebRtcPublisher> &publisher,
											  const std::shared_ptr<pub::Application> &application,
											  const std::shared_ptr<pub::Stream> &stream,
											  const ov::String &file_name,
											  const std::shared_ptr<const SessionDescription> &offer_sdp,
											  const std::shared_ptr<const SessionDescription> &peer_sdp,
											  const std::shared_ptr<IcePort> &ice_port,
											  session_id_t ice_session_id,
											  const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session);

	RtcSession(const info::Session &session_info,
			   const std::shared_ptr<WebRtcPublisher> &publisher,
			   const std::shared_ptr<pub::Application> &application,
			   const std::shared_ptr<pub::Stream> &stream,
			   const ov::String &file_name,
			   const std::shared_ptr<const SessionDescription> &offer_sdp,
			   const std::shared_ptr<const SessionDescription> &peer_sdp,
			   const std::shared_ptr<IcePort> &ice_port,
			   session_id_t ice_session_id,
			   const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session);
	~RtcSession() override;

	bool Start() override;
	bool Stop() override;

	bool RequestChangeRendition(const ov::String &rendition_name);

	enum class SwitchOver : uint8_t
	{
		HIGHER,
		LOWER
	};

	bool RequestChangeRendition(SwitchOver switch_over);
	bool SetAutoAbr(bool auto_abr);

	void SetSessionExpiredTime(uint64_t expired_time);

	const std::shared_ptr<const SessionDescription> &GetPeerSDP() const;
	const std::shared_ptr<const SessionDescription> &GetOfferSDP() const;
	const std::shared_ptr<http::svr::ws::WebSocketSession> &GetWSClient();

	// pub::Session Interface
	void SendOutgoingData(const std::any &packet) override;
	void OnMessageReceived(const std::any &message) override;

	// RtpRtcp Interface
	void OnRtpFrameReceived(const std::vector<std::shared_ptr<RtpPacket>> &rtp_packets) override;
	void OnRtcpReceived(const std::shared_ptr<RtcpInfo> &rtcp_info) override;

	// ov::Node Interface
	bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) override;
	bool OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data) override;

	session_id_t GetIceSessionId() const
	{
		return _ice_session_id;
	}

private:
	bool ProcessReceiverReport(const std::shared_ptr<RtcpInfo> &rtcp_info);
	bool ProcessNACK(const std::shared_ptr<RtcpInfo> &rtcp_info);
	bool ProcessTransportCc(const std::shared_ptr<RtcpInfo> &rtcp_info);
	bool ProcessRemb(const std::shared_ptr<RtcpInfo> &rtcp_info);
	bool IsSelectedPacket(const std::shared_ptr<const RtpPacket> &rtp_packet);

	uint8_t GetOriginPayloadTypeFromRedRtpPacket(const std::shared_ptr<const RedRtpPacket> &red_rtp_packet);

	void ChangeRendition();

	bool SendPlaylistInfo(const std::shared_ptr<const RtcPlaylist> &playlist) const;
	bool SendRenditionChanged(const std::shared_ptr<const RtcRendition> &rendition) const;

	std::shared_ptr<WebRtcPublisher> _publisher;

	std::shared_ptr<RtpRtcp> _rtp_rtcp;
	std::shared_ptr<SrtpTransport> _srtp_transport;
	std::shared_ptr<DtlsTransport> _dtls_transport;

	std::shared_ptr<const SessionDescription> _offer_sdp;
	std::shared_ptr<const SessionDescription> _peer_sdp;
	std::shared_ptr<IcePort> _ice_port;
	std::shared_ptr<http::svr::ws::WebSocketSession> _ws_session;  // Signalling

	uint8_t _video_payload_type	   = 0;
	uint32_t _video_ssrc		   = 0;

	uint8_t _audio_payload_type	   = 0;
	uint32_t _audio_ssrc		   = 0;

	bool _red_enabled			   = false;
	bool _rtx_enabled			   = false;

	uint16_t _rtx_sequence_number  = 1;
	uint64_t _session_expired_time = 0;

	std::shared_mutex _start_stop_lock;

	// For ABR
	ov::String _file_name;
	std::shared_ptr<const RtcPlaylist> _playlist;

	std::shared_ptr<const RtcRendition> _current_rendition = nullptr;
	std::shared_ptr<const RtcRendition> _next_rendition	   = nullptr;
	std::shared_mutex _change_rendition_lock;

	uint16_t _video_rtp_sequence_number = 0;
	uint16_t _audio_rtp_sequence_number = 0;
	uint16_t _wide_sequence_number		= 0;

	ov::StopWatch _abr_test_watch;
	bool _changed = false;

	struct RtpSentLog
	{
		uint16_t _wide_sequence_number	 = 0;
		uint16_t _sequence_number		 = 0;

		uint32_t _track_id				 = 0;
		uint8_t _payload_type			 = 0;
		uint16_t _origin_sequence_number = 0;

		uint32_t _ssrc					 = 0;
		uint32_t _timestamp				 = 0;
		bool _marker					 = false;

		uint32_t _sent_bytes			 = 0;
		std::chrono::system_clock::time_point _sent_time;

		ov::String ToString()
		{
			return ov::String::FormatString("WideSeq(%d) SSRC(%u) Seq(%d) Track(%d) PT(%d) Timestamp(%u) Marker(%s) OriginSeq(%d) SentBytes(%u)",
											_wide_sequence_number, _ssrc, _sequence_number, _track_id, _payload_type, _timestamp, _marker == true ? "O" : "X", _origin_sequence_number, _sent_bytes);
		}
	};

	bool RecordRtpSent(const std::shared_ptr<const RtpPacket> &rtp_packet, uint16_t origin_sequence_number, uint16_t wide_sequence_number);

	std::shared_mutex _rtp_record_map_lock;
	// For NACK
	// video sequence number % MAX_RTP_RECORDS : RtpSentRecord
	std::unordered_map<uint16_t, std::shared_ptr<RtpSentLog>> _video_rtp_sent_record_map;
	// For TRANSPORT-CC
	// wide sequence number % MAX_RTP_RECORDS : RtpSentRecord
	std::unordered_map<uint16_t, std::shared_ptr<RtpSentLog>> _wide_rtp_sent_record_map;

	std::shared_ptr<RtpSentLog> TraceRtpSentByVideoSeqNo(uint16_t sequence_number);
	std::shared_ptr<RtpSentLog> TraceRtpSentByWideSeqNo(uint16_t wide_sequence_number);

	bool SetTransportWideSequenceNumber(const std::shared_ptr<RtpPacket> &rtp_packet, uint16_t wide_sequence_number);
	bool SetAbsSendTime(const std::shared_ptr<RtpPacket> &rtp_packet, uint64_t time_ms);

	// For Estimated bitrate
	double _total_sent_seconds = 0;
	uint64_t _total_sent_bytes = 0;
	double _estimated_bitrates = 0;
	ov::StopWatch _bitrate_estimate_watch;

	// Auto switch rendition
	bool _auto_abr = true;
	void ChangeRenditionIfNeeded();

	// true means Don't know yet
	bool IsNextRenditionGoodChoice(const std::shared_ptr<const RtcRendition> &rendition);
	bool RecordAutoSelectedRendition(const std::shared_ptr<const RtcRendition> &rendition, bool higher_quality);
	// Redition Name, boolean
	struct SelectedRecord
	{
		ov::String _rendition_name;
		uint32_t _selected_count = 0;
		std::chrono::system_clock::time_point _last_selected_time;
	};

	double _previous_estimated_bitrate										 = 0;

	bool _switched_rendition_to_higher										 = false;
	std::chrono::system_clock::time_point _switched_rendition_to_higher_time = {};

	// rendition name, SelectedRecord
	std::map<ov::String, std::shared_ptr<SelectedRecord>> _auto_rendition_selected_records;

	session_id_t _ice_session_id;
};