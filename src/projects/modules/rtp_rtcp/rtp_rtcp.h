#pragma once

#include "rtp_rtcp_defines.h"
#include "rtp_packetizer.h"
#include "base/ovlibrary/node.h"
#include "base/info/media_track.h"
#include "rtcp_info/rtcp_sr_generator.h"
#include "rtcp_info/rtcp_transport_cc_feedback_generator.h"
#include "rtcp_info/sdes.h"
#include "rtcp_info/receiver_report.h"
#include "rtp_frame_jitter_buffer.h"
#include "rtp_minimal_jitter_buffer.h"
#include "rtp_receive_statistics.h"



#define RECEIVER_REPORT_CYCLE_MS	500
#define TRANSPORT_CC_CYCLE_MS		50
#define SDES_CYCLE_MS 500

class RtpRtcpInterface : public ov::EnableSharedFromThis<RtpRtcpInterface>
{
public:
	virtual void OnRtpFrameReceived(const std::vector<std::shared_ptr<RtpPacket>> &rtp_packets) = 0;
	virtual void OnRtcpReceived(const std::shared_ptr<RtcpInfo> &rtcp_info) = 0;
};

class RtpRtcp : public ov::Node
{
public:
	struct RtpTrackIdentifier
	{
	public:
		RtpTrackIdentifier(uint32_t track_id)
			: track_id(track_id)
		{
		}

		uint32_t GetTrackId() const
		{
			return track_id;
		}

		std::optional<uint32_t> ssrc;
		std::optional<uint32_t> interleaved_channel;
		std::optional<ov::String> cname;
		std::optional<ov::String> mid;
		uint32_t mid_extension_id = 0;
		std::optional<ov::String> rid;
		uint32_t rid_extension_id = 0;

	private:
		uint32_t track_id = 0;
	};


	RtpRtcp(const std::shared_ptr<RtpRtcpInterface> &observer);
	~RtpRtcp() override;

	bool AddRtpSender(uint8_t payload_type, uint32_t ssrc, uint32_t codec_rate, ov::String cname);
	bool AddRtpReceiver(const std::shared_ptr<MediaTrack> &track, const RtpTrackIdentifier &rtp_track_id);
	bool Stop() override;

	bool SendRtpPacket(const std::shared_ptr<RtpPacket> &packet);
	bool SendPLI(uint32_t track_id);
	bool SendFIR(uint32_t track_id);

	bool IsTransportCcFeedbackEnabled() const;
	bool EnableTransportCcFeedback(uint8_t extension_id);
	void DisableTransportCcFeedback();

	// These functions help the next node to not have to parse the packet again.
	// Because next node receives raw data format.
	std::shared_ptr<RtpPacket> GetLastSentRtpPacket();
	std::shared_ptr<RtcpPacket> GetLastSentRtcpPacket();

	// Implement Node Interface
	bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) override;
	bool OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data) override;

	std::optional<uint32_t> GetTrackId(uint32_t ssrc) const;
	
private:
	bool OnRtpReceived(NodeType from_node, const std::shared_ptr<const ov::Data> &data);
	bool OnRtcpReceived(NodeType from_node, const std::shared_ptr<const ov::Data> &data);

	std::shared_ptr<RtpFrameJitterBuffer> GetJitterBuffer(uint8_t payload_type);

	std::shared_ptr<RtcpPacket> GenerateTransportCcFeedbackIfNeeded();

	std::vector<RtpTrackIdentifier> _rtp_track_identifiers;
	std::map<uint32_t /*ssrc*/, uint32_t /*track_id*/> _ssrc_to_track_id;

	// Find track id by mid or rid
	std::optional<uint32_t> FindTrackId(const std::shared_ptr<const RtpPacket> &rtp_packet) const;
	// Find track id by SDES
	std::optional<uint32_t> FindTrackId(const std::shared_ptr<const Sdes> &sdes) const;
	// Find track id by rtsp channel id
	std::optional<uint32_t> FindTrackId(uint8_t rtsp_inter_channel) const;

	void ConnectSsrcToTrack(uint32_t ssrc, uint32_t track_id);

    time_t _first_receiver_report_time = 0; // 0 - not received RR packet
    time_t _last_sender_report_time = 0;
    uint64_t _send_packet_sequence_number = 0;

	std::shared_mutex _state_lock;
	std::shared_ptr<RtpRtcpInterface> _observer;
    std::map<uint32_t, std::shared_ptr<RtcpSRGenerator>> _rtcp_sr_generators;
	std::shared_ptr<Sdes> _sdes = nullptr;
	std::shared_ptr<RtcpPacket> _rtcp_sdes = nullptr;
	ov::StopWatch _rtcp_send_stop_watch;
	uint64_t _rtcp_sent_count = 0;

	bool _transport_cc_feedback_enabled = false;
	uint8_t _transport_cc_feedback_extension_id = 0;
	
	// Receiver SSRC (For RTCP RR, FIR... etc)
	// track_id : Receiver Statistics
	std::unordered_map<uint32_t, std::shared_ptr<RtpReceiveStatistics>> _receive_statistics;

	// Transport-cc feedback
	std::shared_ptr<RtcpTransportCcFeedbackGenerator> _transport_cc_generator = nullptr;

	// Jitter buffer
	// payload type : Jitter buffer
	std::unordered_map<uint8_t, std::shared_ptr<RtpFrameJitterBuffer>> _rtp_frame_jitter_buffers;
	std::unordered_map<uint8_t, std::shared_ptr<RtpMinimalJitterBuffer>> _rtp_minimal_jitter_buffers;

	// payload type : MediaTrack Info
	std::unordered_map<uint8_t, std::shared_ptr<MediaTrack>> _tracks;
	bool _video_receiver_enabled = false;
	bool _audio_receiver_enabled = false;

	// Latest packet
	std::shared_ptr<RtpPacket>		_last_sent_rtp_packet = nullptr;
	std::shared_ptr<RtcpPacket>		_last_sent_rtcp_packet = nullptr;
};
