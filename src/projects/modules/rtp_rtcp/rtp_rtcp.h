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

#include <atomic>
#include <mutex>
#include <shared_mutex>


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
	std::shared_ptr<RtpMinimalJitterBuffer> GetMinimalJitterBuffer(uint8_t payload_type);
	std::shared_ptr<MediaTrack> GetTrack(uint32_t track_id) const;

	std::shared_ptr<RtpReceiveStatistics> GetOrCreateReceiveStatistics(uint32_t track_id, uint32_t ssrc, uint32_t clock_rate);
	std::shared_ptr<RtpReceiveStatistics> FindReceiveStatistics(uint32_t track_id) const;

	// Returns the transport-cc feedback packet to send (nullptr if not due yet)
	std::shared_ptr<RtcpPacket> GenerateTransportCcFeedbackIfNeeded(const std::shared_ptr<RtpPacket> &packet, uint32_t receiver_ssrc, bool is_video, bool marker);

	void SetLastSentRtpPacket(const std::shared_ptr<RtpPacket> &packet);
	void SetLastSentRtcpPacket(const std::shared_ptr<RtcpPacket> &packet);

	// _ssrc_to_track_id_lock guards _ssrc_to_track_id
	std::map<uint32_t /*ssrc*/, uint32_t /*track_id*/> _ssrc_to_track_id;
	mutable std::shared_mutex _ssrc_to_track_id_lock;

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

	// Lifecycle gate: data path (send/receive) takes it shared, Stop/setup exclusive
	std::shared_mutex _state_lock;
	std::shared_ptr<RtpRtcpInterface> _observer;

	// _rtcp_send_state_lock guards the send-side RTCP state below
	std::map<uint32_t, std::shared_ptr<RtcpSRGenerator>> _rtcp_sr_generators;
	std::shared_ptr<Sdes> _sdes = nullptr;
	std::shared_ptr<RtcpPacket> _rtcp_sdes = nullptr;
	ov::StopWatch _rtcp_send_stop_watch;
	uint64_t _rtcp_sent_count = 0;
	mutable std::shared_mutex _rtcp_send_state_lock;

	std::atomic<bool> _transport_cc_feedback_enabled = false;
	std::atomic<uint8_t> _transport_cc_feedback_extension_id = 0;

	// _receive_statistics_lock guards _receive_statistics (track_id : receiver statistics)
	std::unordered_map<uint32_t, std::shared_ptr<RtpReceiveStatistics>> _receive_statistics;
	mutable std::shared_mutex _receive_statistics_lock;

	// _transport_cc_generator_lock guards _transport_cc_generator
	std::shared_ptr<RtcpTransportCcFeedbackGenerator> _transport_cc_generator = nullptr;
	mutable std::shared_mutex _transport_cc_generator_lock;

	// _track_info_lock guards the receive-setup containers below
	std::vector<RtpTrackIdentifier> _rtp_track_identifiers;
	std::unordered_map<uint8_t, std::shared_ptr<RtpFrameJitterBuffer>> _rtp_frame_jitter_buffers;
	std::unordered_map<uint8_t, std::shared_ptr<RtpMinimalJitterBuffer>> _rtp_minimal_jitter_buffers;
	std::unordered_map<uint8_t, std::shared_ptr<MediaTrack>> _tracks;
	mutable std::shared_mutex _track_info_lock;

	std::atomic<bool> _video_receiver_enabled = false;
	std::atomic<bool> _audio_receiver_enabled = false;

	// _last_sent_packet_lock guards both last-sent packet pointers below
	std::shared_ptr<RtpPacket>		_last_sent_rtp_packet = nullptr;
	std::shared_ptr<RtcpPacket>		_last_sent_rtcp_packet = nullptr;
	mutable std::shared_mutex _last_sent_packet_lock;
};
