//==============================================================================
//
//  WebRTC Provider
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/provider/push_provider/stream.h>

#include "modules/ice/ice_port.h"
#include "modules/sdp/session_description.h"

#include "modules/rtp_rtcp/rtp_rtcp.h"
#include "modules/rtp_rtcp/rtp_packetizer_interface.h"
#include "modules/dtls_srtp/dtls_transport.h"
#include "modules/rtp_rtcp/rtp_depacketizing_manager.h"

#include "modules/bitstream/h264/h264_bitstream_parser.h"

#include "modules/access_control/request_info.h"

namespace pvd
{
	class WebRTCStream final : public pvd::PushStream, public RtpRtcpInterface, public ov::Node
	{
	public:
		static std::shared_ptr<WebRTCStream> Create(StreamSourceType source_type, ov::String stream_name,
													const std::shared_ptr<PushProvider> &provider,
													const std::shared_ptr<const SessionDescription> &local_sdp,
													const std::shared_ptr<const SessionDescription> &remote_sdp,
													const std::shared_ptr<Certificate> &certificate, 
													const std::shared_ptr<IcePort> &ice_port,
													session_id_t ice_session_id, 
													const std::shared_ptr<const ac::RequestInfo> &request_info,
													const cfg::vhost::app::pvd::WebrtcProvider &config);
		
		explicit WebRTCStream(StreamSourceType source_type, ov::String stream_name, 
								const std::shared_ptr<PushProvider> &provider,
								const std::shared_ptr<const SessionDescription> &local_sdp,
								const std::shared_ptr<const SessionDescription> &remote_sdp,
								const std::shared_ptr<Certificate> &certificate, 
								const std::shared_ptr<IcePort> &ice_port,
								session_id_t ice_session_id, 
								const std::shared_ptr<const ac::RequestInfo> &request_info,
								const cfg::vhost::app::pvd::WebrtcProvider &config);
		~WebRTCStream() final;

		bool Start() override;
		bool Stop() override;

		std::shared_ptr<const SessionDescription> GetLocalSDP();
		std::shared_ptr<const SessionDescription> GetPeerSDP();

		// Get the session key of the stream
		ov::String GetSessionKey() const;

		session_id_t GetIceSessionId() const
		{
			return _ice_session_id;
		}

		std::shared_ptr<const ac::RequestInfo> GetRequestInfo() const
		{
			return _request_info;
		}

		// ------------------------------------------
		// Implementation of PushStream
		// ------------------------------------------
		PushStreamType GetPushStreamType() override
		{
			return PushStream::PushStreamType::DATA;
		}
		bool OnDataReceived(const std::shared_ptr<const ov::Data> &data) override;

		// RtpRtcpInterface Implement
		void OnRtpFrameReceived(const std::vector<std::shared_ptr<RtpPacket>> &rtp_packets) override;
		void OnRtcpReceived(const std::shared_ptr<RtcpInfo> &rtcp_info) override;

		// ov::Node Interface
		bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) override;
		bool OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data) override;

		void SetOvenCapabilities(const ov::String &capabilities);

	private:
		bool CreateChannel(const std::shared_ptr<const MediaDescription> &local_media_desc, 
							const std::shared_ptr<const MediaDescription> &remote_media_desc,
							const std::shared_ptr<const RidAttr> &rid_attr, /* Optional / can be nullptr */
							const std::shared_ptr<const PayloadAttr> &payload_attr);

		std::shared_ptr<MediaTrack> CreateTrack(const std::shared_ptr<const PayloadAttr> &payload_attr);
		bool AddDepacketizer(uint32_t track_id);
		std::shared_ptr<RtpDepacketizingManager> GetDepacketizer(uint32_t track_id);

		void OnFrame(const std::shared_ptr<MediaTrack> &track, const std::shared_ptr<MediaPacket> &media_packet);

		ov::StopWatch _fir_timer;
		int _fir_interval = 3000; // ms

		ov::String _session_key;

		std::shared_ptr<const SessionDescription> _local_sdp;
		std::shared_ptr<const SessionDescription> _remote_sdp;
		std::shared_ptr<const SessionDescription> _offer_sdp;
		std::shared_ptr<const SessionDescription> _answer_sdp;

		std::shared_ptr<IcePort> _ice_port;
		std::shared_ptr<Certificate> _certificate;

		std::shared_ptr<RtpRtcp>            _rtp_rtcp;
		std::shared_ptr<SrtpTransport>      _srtp_transport;
		std::shared_ptr<DtlsTransport>      _dtls_transport;

		bool								_rtx_enabled = false;
		std::shared_mutex					_start_stop_lock;

		// CompositionTime extmap
		bool _cts_extmap_enabled = false;
		uint8_t _cts_extmap_id = 0;
		std::map<int64_t, std::shared_ptr<MediaPacket>> _dts_ordered_frame_buffer;
		H264BitstreamParser _h264_bitstream_parser;

		// Track ID, Depacketizer
		std::map<uint32_t, std::shared_ptr<RtpDepacketizingManager>> _depacketizers;

		std::map<uint32_t, std::shared_ptr<ov::Data>> _h26x_extradata_nalu;
		std::map<uint32_t, bool> _sent_sequence_header;

		ov::String _oven_capabilities;

		session_id_t _ice_session_id = 0;

		std::shared_ptr<const ac::RequestInfo> _request_info;
	};
}