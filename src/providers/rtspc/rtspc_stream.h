//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/url.h>

#include <base/provider/pull_provider/stream.h>
#include <base/provider/pull_provider/application.h>

#include <modules/rtsp/rtsp_message.h>
#include <modules/rtsp/rtsp_demuxer.h>

#include <modules/rtp_rtcp/rtp_depacketizing_manager.h>
#include <modules/rtp_rtcp/rtp_rtcp.h>

#include <modules/sdp/session_description.h>

#include <modules/rtsp/header_fields/rtsp_header_fields.h>

#include <atomic>

#define RTSP_USER_AGENT_NAME				"OvenMediaEngine"
#define DEFAULT_RTSP_SESSION_TIMEOUT_SEC	30
namespace pvd
{
	class RtspcProvider;

	class RtspcStream final : public pvd::PullStream, public RtpRtcpInterface, public ov::Node
	{
		// Unit-test access to the stop path (see `rtspc_test.cpp`)
		friend class RtspcStreamTest;

	public:
		static std::shared_ptr<RtspcStream> Create(const std::shared_ptr<pvd::PullApplication> &application, const uint32_t stream_id, const ov::String &stream_name, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);

		RtspcStream(const std::shared_ptr<pvd::PullApplication> &application, const info::Stream &stream_info, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);
		~RtspcStream() final;

		ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() override {
			return ProcessMediaEventTrigger::TRIGGER_EPOLL;
		}
		// PullStream Implementation
		std::shared_ptr<ov::Socket> GetSocketForDetectingEvent() override;
		// If this stream belongs to the Pull provider, 
		// this function is called periodically by the StreamMotor of application. 
		// Media data has to be processed here.
		PullStream::ProcessMediaResult ProcessMediaPacket() override;

		// RtpRtcpInterface Implementation
		void OnRtpFrameReceived(const std::vector<std::shared_ptr<RtpPacket>> &rtp_packets) override;
		void OnRtcpReceived(const std::shared_ptr<RtcpInfo> &rtcp_info) override;

		// ov::Node Interface
		bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) override;
		bool OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data) override;

	private:
		std::shared_ptr<pvd::RtspcProvider> GetRtspcProvider();

		bool StartStream(const std::shared_ptr<const ov::Url> &url) override; // Start
		bool RestartStream(const std::shared_ptr<const ov::Url> &url) override; // Failover
		bool StopStream() override; // Stop

		bool ConnectTo();
		bool RequestDescribe();
		bool RequestSetup();
		bool RequestPlay();
		void RequestStop() OV_REQUIRES(_connection_lock);
		void Release();

		bool Ping() OV_REQUIRES(_connection_lock); // Send GET_PARAMETER

		int32_t GetNextCSeq();

		bool SendRequestMessage(const std::shared_ptr<RtspMessage> &message);
		// Reads the response directly from the socket: only the handshake before `PLAYING` waits for one
		std::shared_ptr<RtspMessage> ReceiveResponse(uint32_t cseq, uint64_t timeout_ms);

		// Blocking, it is used before playing state
		std::shared_ptr<RtspMessage> ReceiveMessage(int64_t timeout_msec);

		// Receive and append packet to demuxer
		bool ReceivePacket(bool non_block = false, int64_t timeout_msec = 0);

		bool AddDepacketizer(uint8_t payload_type, RtpDepacketizingManager::SupportedDepacketizerType codec_id);
		std::shared_ptr<RtpDepacketizingManager> GetDepacketizer(uint8_t payload_type);

		ov::String GenerateControlUrl(ov::String control);

		std::vector<std::shared_ptr<const ov::Url>> _url_list;
		std::shared_ptr<const ov::Url> _curr_url;
		std::shared_ptr<RtspHeaderAuthorizationField> _authorization_field = nullptr;

		std::shared_ptr<ov::Socket> _signalling_socket;

		// Held by `ProcessMediaPacket()` on the motor's worker, and by a stop, a start and the end of a handshake,
		// so the connection is never released or replaced while its media is being read
		ov::Mutex _connection_lock;
		// Whether `ProcessMediaPacket()` may use the connection: set once a handshake reaches `PLAYING`,
		// cleared by a stop or a start
		bool _connection_ready OV_GUARDED_BY(_connection_lock) = false;
		// Set by a stop or a start before it takes `_connection_lock`, and cleared once a handshake reaches `PLAYING`.
		// While set, `ProcessMediaPacket()` returns without the lock, so the motor's worker cannot keep retaking it.
		std::atomic<bool> _connection_released = false;

		// Values from RTSP
		int32_t	_cseq = 0;

		SessionDescription _sdp;

		ov::String _content_base;
		ov::String _rtsp_session_id;
		uint32_t _rtsp_session_timeout_sec = 0;
		std::shared_ptr<ov::Data> _h264_extradata_nalu = nullptr;
		std::shared_ptr<ov::Data> _h265_extradata_nalu = nullptr;
		// ssrc, rtp channel id (rtcp channel id = rtp_channel_id + 1)
		std::map<uint32_t, uint8_t> _ssrc_channel_id_map;

		RtspDemuxer _rtsp_demuxer;

		// Rtp
		std::shared_ptr<RtpRtcp>            _rtp_rtcp;
		// Payload type, Depacketizer
		std::map<uint8_t, std::shared_ptr<RtpDepacketizingManager>> _depacketizers;
		// Payload type : Timestamp
		std::map<uint8_t, uint32_t>			_last_timestamp_map;
		std::map<uint8_t, uint32_t>			_timestamp_map;

		bool _sent_sequence_header = false;

		// Statistics
		int64_t _origin_request_time_msec = 0;
		int64_t _origin_response_time_msec = 0;

		ov::StopWatch _ping_timer;
	};
}