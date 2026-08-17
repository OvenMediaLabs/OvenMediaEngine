#pragma once

#include "rtp_packet.h"
#include "rtp_packetizing_manager.h"
#include "rtp_rtcp_defines.h"
#include "ulpfec_generator.h"

#include "base/mediarouter/media_buffer.h"
#include <memory>

#include "rtp_header_extension/rtp_header_extensions.h"
#include "rtp_header_extension/rtp_header_extension_framemarking.h"
#include "rtp_header_extension/rtp_header_extension_playout_delay.h"
#include "rtp_header_extension/rtp_header_extension_transport_cc.h"
#include "rtp_header_extension/rtp_header_extension_abs_send_time.h"

class RtpPacketizer
{
public:
	RtpPacketizer(const std::shared_ptr<RtpPacketizerInterface> &session);
	~RtpPacketizer();

	bool SetCodec(cmn::MediaCodecId codec_type);
	void SetUlpfec(uint8_t _red_payload_type, uint8_t _ulpfec_payload_type);
	void SetTrackId(uint32_t track_id);
	void SetPayloadType(uint8_t payload_type);
	void SetSSRC(uint32_t ssrc);
	void SetCsrcs(const std::vector<uint32_t> &csrcs);
	void SetPlayoutDelay(uint32_t min, uint32_t max);
	void EnableTransportCc(uint16_t dummy_seq_num);
	void EnableAbsSendTime();

	// RTP Packet
	bool Packetize(FrameType frame_type,
	               uint32_t rtp_timestamp,
				   uint64_t ntp_timestamp,
	               const uint8_t *payload_data,
	               size_t payload_size,
	               const FragmentationHeader *fragmentation,
	               const RTPVideoHeader *rtp_header);

private:
	void SetVideoCodec(cmn::MediaCodecId codec_type);
	void SetAudioCodec(cmn::MediaCodecId codec_type);
	// Basic
	std::shared_ptr<RtpPacket> AllocatePacket(bool ulpfec=false);
	std::shared_ptr<RedRtpPacket> PackageAsRed(std::shared_ptr<RtpPacket> rtp_packet);
	bool AssignSequenceNumber(RtpPacket *packet, bool red = false);
	bool MarkerBit(FrameType frame_type, int8_t payload_type);

	// Video Packet Sender Interface
	bool PacketizeVideo(cmn::MediaCodecId video_type,
	                    FrameType frame_type,
	                    uint32_t rtp_timestamp,
						uint64_t ntp_timestamp,
	                    const uint8_t *payload_data,
	                    size_t payload_size,
	                    const FragmentationHeader *fragmentation,
	                    const RTPVideoHeader *video_header);

	bool GenerateRedAndFecPackets(std::shared_ptr<RtpPacket> packet);

	// Audio Pakcet Sender Interface
	bool PacketizeAudio(FrameType frame_type,
	                    uint32_t rtp_timestamp,
						uint64_t ntp_timestamp,
	                    const uint8_t *payload_data,
	                    size_t payload_size);

	// Member
	uint32_t _timestamp_offset;
	bool _audio_configured;

	// Session Information
	uint32_t _track_id;
	uint32_t _ssrc;
	uint8_t _payload_type;
	std::vector<uint32_t> _csrcs;
	// Sequence Number
	uint16_t _sequence_number;
	uint16_t _red_sequence_number;

	bool _ulpfec_enabled;
	uint8_t _red_payload_type;
	uint8_t _ulpfec_payload_type;

	UlpfecGenerator _ulpfec_generator;

	cmn::MediaCodecId		_video_codec_type;
	cmn::MediaCodecId		_audio_codec_type;
	std::shared_ptr<RtpPacketizingManager> _packetizer = nullptr;

	uint64_t		_frame_count = 0;
	uint64_t		_rtp_packet_count = 0;

	RtpHeaderExtensions _rtp_extensions;
	std::shared_ptr<RtpHeaderExtensionFrameMarking>	_framemarking_extension;
	std::shared_ptr<RtpHeaderExtensionPlayoutDelay> _playout_delay_extension;
	std::shared_ptr<RtpHeaderExtensionTransportCc> _transport_cc_extension;
	std::shared_ptr<RtpHeaderExtensionAbsSendTime> _abs_send_time_extension;

	// Session Descriptor
	std::shared_ptr<RtpPacketizerInterface> _stream;
};
