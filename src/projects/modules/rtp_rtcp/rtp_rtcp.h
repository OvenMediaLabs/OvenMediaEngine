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
#include "rtp_nack_generator.h"
#include "rtp_frame_boundary_detector.h"

#include <mutex>



#define RECEIVER_REPORT_CYCLE_MS	500
#define TRANSPORT_CC_CYCLE_MS		50
#define SDES_CYCLE_MS 500
#define NACK_COALESCE_MS			10

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
		// Extension id for `repaired-rtp-stream-id` carried on RTX packets
		// (libwebrtc simulcast). When set, FindTrackId(packet) also matches
		// mid + rrid where rrid equals this track's `rid`.
		uint32_t rrid_extension_id = 0;

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

	// Enable receive-side NACK for the given track. Creates a per-track
	// RtpNackGenerator that observes incoming sequence numbers and drives
	// outbound NACK feedback. max_hold_ms is the upper bound for the
	// jitter buffer hold time recommendation (operator-tunable latency budget).
	bool EnableNack(uint32_t track_id, uint32_t media_ssrc, uint32_t max_hold_ms);

	// Register an RTX stream so that RTP packets arriving on rtx_ssrc with
	// rtx_payload_type are unwrapped (RFC 4588) and re-injected as original
	// (media_ssrc, original_payload_type) before depacketization.
	bool RegisterRtxStream(uint32_t rtx_ssrc, uint32_t media_ssrc,
						   uint8_t rtx_payload_type, uint8_t original_payload_type);

	// Register an RTX payload type pairing. When an RTP packet arrives with
	// this payload type on an unknown SSRC (typical for simulcast WHIP where
	// browsers don't declare ssrc-group:FID in SDP), it is detected as RTX,
	// the original track is resolved via mid/rid extension, and the RTX SSRC
	// is cached for subsequent packets.
	bool RegisterRtxPayloadType(uint8_t rtx_payload_type, uint8_t original_payload_type);

	// Register negotiated Dependency Descriptor extension id (video m-line scope).
	// Call only when DD was negotiated; otherwise the detector falls back to
	// the codec's RTP payload header parse.
	bool SetDependencyDescriptorExtId(uint8_t dd_extension_id);

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

	bool SendNACK(uint32_t track_id, const std::vector<uint16_t> &lost_ids);

	// Coalesced NACK flush for a single track. Sends if the coalescing
	// window has elapsed; otherwise no-op.
	void FlushNackIfDue(uint32_t track_id, const std::shared_ptr<RtpNackGenerator> &generator);

	// RTX (RFC 4588) detection + unwrap. On entry, packet is the raw RTP
	// packet from the wire. On Unwrapped return, packet has been replaced
	// with the original media packet (PT/SSRC/seq restored). Drop indicates
	// the input was RTX but should not be processed further (padding-only
	// probe, PT mismatch, unresolved track).
	enum class RtxResult { NotRtx, Unwrapped, Drop };
	RtxResult TryUnwrapRtx(std::shared_ptr<RtpPacket> &packet);

	std::shared_ptr<RtpFrameJitterBuffer> GetJitterBuffer(uint32_t track_id);

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

	// Per-track receive-side NACK generator.
	std::unordered_map<uint32_t, std::shared_ptr<RtpNackGenerator>> _nack_generators;
	std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> _last_nack_flush_at;

	// _last_nack_flush_at is updated on the receive path (FlushNackIfDue) under
	// only a shared_lock on _state_lock, so it needs its own mutex to serialize
	// concurrent receives (operator[] can insert/rehash).
	std::mutex _last_nack_flush_at_lock;

	// Negotiated DD extension id (0 = not negotiated). The detector falls
	// back to codec payload parse when 0.
	uint8_t _dd_extension_id = 0;

	// RTX SSRC -> {media_ssrc, rtx_pt, original_pt}
	struct RtxStreamInfo
	{
		uint32_t media_ssrc;
		uint8_t rtx_payload_type;
		uint8_t original_payload_type;
	};
	std::unordered_map<uint32_t /*rtx_ssrc*/, RtxStreamInfo> _rtx_streams;

	// _rtx_streams is learned on the receive path (TryUnwrapRtx) while only a
	// shared_lock on _state_lock is held, so it needs its own mutex: a shared
	// lock allows concurrent readers, and a map write racing a reader is UB.
	std::mutex _rtx_streams_lock;

	// rtx_pt -> original_pt, set up from negotiated SDP to detect RTX packets
	// dynamically when the RTX SSRC isn't pre-declared (simulcast WHIP).
	std::unordered_map<uint8_t /*rtx_pt*/, uint8_t /*original_pt*/> _rtx_pt_to_original;

	// Transport-cc feedback
	std::shared_ptr<RtcpTransportCcFeedbackGenerator> _transport_cc_generator = nullptr;

	// Jitter buffer
	// track_id : Jitter buffer
	std::unordered_map<uint32_t, std::shared_ptr<RtpFrameJitterBuffer>> _rtp_frame_jitter_buffers;
	std::unordered_map<uint32_t, std::shared_ptr<RtpMinimalJitterBuffer>> _rtp_minimal_jitter_buffers;

	// track_id : MediaTrack Info
	std::unordered_map<uint32_t, std::shared_ptr<MediaTrack>> _tracks;
	bool _video_receiver_enabled = false;
	bool _audio_receiver_enabled = false;

	// Latest packet
	std::shared_ptr<RtpPacket>		_last_sent_rtp_packet = nullptr;
	std::shared_ptr<RtcpPacket>		_last_sent_rtcp_packet = nullptr;
};
