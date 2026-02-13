#include <time.h>
#include <iostream>
#include <memory>
#include "rtp_packet.h"
#include "red_rtp_packet.h"
#include "rtp_packetizing_manager.h"
#include "rtp_packetizer.h"

#define OV_LOG_TAG "RtpRtcp"

RtpPacketizer::RtpPacketizer(const std::shared_ptr<RtpPacketizerInterface> &session)
{
	_stream = session;

	_audio_configured = false;
	_timestamp_offset = (uint32_t)rand();
	_sequence_number = (uint16_t)rand();
	_red_sequence_number = (uint16_t)rand();
	_ulpfec_enabled = false;
}

RtpPacketizer::~RtpPacketizer()
{

}

bool RtpPacketizer::SetCodec(cmn::MediaCodecId codec_type)
{
	switch (codec_type)
	{
		case cmn::MediaCodecId::H264:
			SetVideoCodec(cmn::MediaCodecId::H264);
			break;
		case cmn::MediaCodecId::Vp8:
			SetVideoCodec(cmn::MediaCodecId::Vp8);
			break;
		case cmn::MediaCodecId::H265:
			SetVideoCodec(cmn::MediaCodecId::H265);
			break;
		case cmn::MediaCodecId::Opus:
			SetAudioCodec(cmn::MediaCodecId::Opus);
			break;
		default:
			// Unsupported Codec
			return false;
	}

	return true;
}

void RtpPacketizer::SetVideoCodec(cmn::MediaCodecId codec_type)
{
	_audio_configured = false;
	_video_codec_type = codec_type;
	_packetizer = RtpPacketizingManager::Create(codec_type);

	// RTP Extension
	_framemarking_extension = std::make_shared<RtpHeaderExtensionFrameMarking>();
	_rtp_extensions.AddExtention(_framemarking_extension);
}

void RtpPacketizer::SetAudioCodec(cmn::MediaCodecId codec_type)
{
	_audio_codec_type = codec_type;
	_audio_configured = true;
}

void RtpPacketizer::SetPlayoutDelay(uint32_t min, uint32_t max)
{
	_playout_delay_extension = std::make_shared<RtpHeaderExtensionPlayoutDelay>();
	_playout_delay_extension->SetDelayMilliseconds(min, max);
	_rtp_extensions.AddExtention(_playout_delay_extension);
}

void RtpPacketizer::EnableTransportCc(uint16_t dummy_seq_num)
{
	_transport_cc_extension = std::make_shared<RtpHeaderExtensionTransportCc>();
	// The transport-wide sequence number will be set just before transmission in the session.
	_transport_cc_extension->SetSequenceNumber(dummy_seq_num);
	_rtp_extensions.AddExtention(_transport_cc_extension);
}

void RtpPacketizer::EnableAbsSendTime()
{
	_abs_send_time_extension = std::make_shared<RtpHeaderExtensionAbsSendTime>();
	_abs_send_time_extension->SetAbsSendTimeMs(ov::Clock::NowMSec());
	_rtp_extensions.AddExtention(_abs_send_time_extension);
}

void RtpPacketizer::SetTrackId(uint32_t track_id)
{
	_track_id = track_id;
}

void RtpPacketizer::SetPayloadType(uint8_t payload_type)
{
	_payload_type = payload_type;
}

void RtpPacketizer::SetSSRC(const uint32_t ssrc)
{
	_ssrc = ssrc;
}

void RtpPacketizer::SetCsrcs(const std::vector<uint32_t> &csrcs)
{
	_csrcs = csrcs;
}

void RtpPacketizer::SetUlpfec(uint8_t red_payload_type, uint8_t ulpfec_payload_type)
{
	_ulpfec_enabled = true;
	_red_payload_type = red_payload_type;
	_ulpfec_payload_type = ulpfec_payload_type;
}

bool RtpPacketizer::Packetize(FrameType frame_type,
                                   uint32_t rtp_timestamp,
								   uint64_t ntp_timestamp,
                                   const uint8_t *payload_data,
                                   size_t payload_size,
                                   const FragmentationHeader *fragmentation,
                                   const RTPVideoHeader *rtp_header)
{
	_frame_count ++;

	if(_audio_configured)
	{
		return PacketizeAudio(frame_type, rtp_timestamp, ntp_timestamp, payload_data, payload_size);
	}
	else
	{
		return PacketizeVideo(rtp_header->codec, frame_type, rtp_timestamp, ntp_timestamp, payload_data, payload_size, fragmentation, rtp_header);
	}
}

bool RtpPacketizer::PacketizeVideo(cmn::MediaCodecId video_type,
                                   FrameType frame_type,
                                   uint32_t rtp_timestamp,
								   uint64_t ntp_timestamp,
                                   const uint8_t *payload_data,
                                   size_t payload_size,
                                   const FragmentationHeader *fragmentation,
                                   const RTPVideoHeader *video_header)
{

	std::shared_ptr<RtpPacket> rtp_header_template, red_rtp_header_template;
	std::shared_ptr<RtpPacket> last_rtp_header, last_red_rtp_header;

	rtp_header_template = AllocatePacket();
	rtp_header_template->SetTimestamp(rtp_timestamp);

	last_rtp_header = AllocatePacket();
	last_rtp_header->SetTimestamp(rtp_timestamp);


	// TODO: Add following extentions
	// - Rotation Extension
	// - Video Content Type Extension
	// - Video Timing Extension

	// -20 is for FEC
	size_t max_data_payload_length = RTP_DEFAULT_MAX_PACKET_SIZE - rtp_header_template->HeadersSize() - 100;
	size_t last_packet_reduction_len = last_rtp_header->HeadersSize() - rtp_header_template->HeadersSize();
														
	if(_packetizer == nullptr)
	{
		logte("Cannot create _packetizers");
		return false;
	}

	size_t num_packets = _packetizer->SetPayloadData(max_data_payload_length, last_packet_reduction_len, video_header ? &(video_header->codec_header) : nullptr, frame_type, 
													payload_data, payload_size, fragmentation);
	if(num_packets == 0)
	{
		//logte("Packetizer returns 0 packet");
		return false;
	}

	for(size_t i = 0; i < num_packets; ++i)
	{
		bool last = (i + 1) == num_packets;
		auto packet = last ? std::move(last_rtp_header) : std::make_shared<RtpPacket>(*rtp_header_template);

		if(!AssignSequenceNumber(packet.get()))
		{
			return false;
		}

		packet->SetVideoPacket(true);

		_framemarking_extension->Reset();

		if(i == 0)
		{
			packet->SetFirstPacketOfFrame(true);
			_framemarking_extension->SetStartOfFrame();
		}
		else if(last == true)
		{
			_framemarking_extension->SetEndOfFrame();
		}

		if(frame_type == FrameType::VideoFrameKey)
		{
			packet->SetKeyframe(true);
			_framemarking_extension->SetIndependentFrame();
		}

		packet->SetNTPTimestamp(ntp_timestamp);
		packet->SetTrackId(_track_id);
		packet->SetExtensions(_rtp_extensions);

		// Set Payload
		if(!_packetizer->NextPacket(packet.get()))
		{
			return false;
		}

		_rtp_packet_count ++;
		_stream->OnRtpPacketized(packet);

		// RED First
		if(_ulpfec_enabled)
		{
			GenerateRedAndFecPackets(packet);
		}
	}

	return true;
}

bool RtpPacketizer::GenerateRedAndFecPackets(std::shared_ptr<RtpPacket> packet)
{
	// Send RED
	auto red_packet = PackageAsRed(packet);

	// Separate the sequence number of the RED packet from RTP packet.
	// Because FEC packet should not affect the sequence number of the RTP packet.
	AssignSequenceNumber(red_packet.get(), true);

	_ulpfec_generator.AddRtpPacketAndGenerateFec(red_packet);
	red_packet->PackageAsRtp();
	_stream->OnRtpPacketized(red_packet);

	while(_ulpfec_generator.IsAvailableFecPackets())
	{
		auto red_fec_packet = AllocatePacket(true);

		// RED packet is for only video 
		red_fec_packet->SetVideoPacket(true);

		// Timestamp is same as last packet
		red_fec_packet->SetTimestamp(packet->Timestamp());

		// Sequence Number
		AssignSequenceNumber(red_fec_packet.get(), true);

		_ulpfec_generator.NextPacket(red_fec_packet.get());

		// Adjust completed red packet's payload offset and size for RTX packet
		std::dynamic_pointer_cast<RedRtpPacket>(red_fec_packet)->PackageAsRtp();

		// Send ULPFEC
		_rtp_packet_count ++;
		_stream->OnRtpPacketized(red_fec_packet);
	}

	return true;
}

bool RtpPacketizer::PacketizeAudio(FrameType frame_type,
                                   uint32_t rtp_timestamp,
								   uint64_t ntp_timestamp,
                                   const uint8_t *payload_data,
                                   size_t payload_size)
{
	if(payload_size == 0 || payload_data == nullptr)
	{
		if(frame_type == FrameType::EmptyFrame)
		{
			// Don't need to send empty packet
			return true;
		}

		return false;
	}

	std::shared_ptr<RtpPacket> packet = AllocatePacket();
	packet->SetMarker(MarkerBit(frame_type, _payload_type));
	packet->SetPayloadType(_payload_type);
	packet->SetTimestamp(rtp_timestamp);
	packet->SetNTPTimestamp(ntp_timestamp);
	packet->SetTrackId(_track_id);
	packet->SetExtensions(_rtp_extensions);

	uint8_t *payload = packet->AllocatePayload(payload_size);

	if(payload == nullptr)
	{
		OV_ASSERT2(false);
		return false;
	}

	::memcpy(payload, payload_data, payload_size);

	if(AssignSequenceNumber(packet.get()) == false)
	{
		OV_ASSERT2(false);
		return false;
	}

	// logt("RtpSender.Packet", "Trying to send packet:\n%s", packet->GetData()->Dump().CStr());

	_stream->OnRtpPacketized(packet);

	return true;
}

std::shared_ptr<RedRtpPacket> RtpPacketizer::PackageAsRed(std::shared_ptr<RtpPacket> rtp_packet)
{
	return std::make_shared<RedRtpPacket>(_red_payload_type, *rtp_packet);
}

std::shared_ptr<RtpPacket> RtpPacketizer::AllocatePacket(bool ulpfec)
{
	if(ulpfec)
	{
		auto red_packet = std::make_shared<RedRtpPacket>();
		red_packet->SetSsrc(_ssrc);
		red_packet->SetCsrcs(_csrcs);
		red_packet->SetPayloadType(_ulpfec_payload_type);
		red_packet->SetUlpfec(true, _payload_type);
		red_packet->PackageAsRed(_red_payload_type);
		
		return red_packet;
	}
	else
	{
		auto rtp_packet = std::make_shared<RtpPacket>();
		rtp_packet->SetSsrc(_ssrc);
		rtp_packet->SetCsrcs(_csrcs);
		rtp_packet->SetPayloadType(_payload_type);

		return rtp_packet;
	}
}

bool RtpPacketizer::AssignSequenceNumber(RtpPacket *packet, bool red)
{
	if(!red)
	{
		packet->SetSequenceNumber(_sequence_number++);
	}
	else
	{
		packet->SetSequenceNumber(_red_sequence_number++);
	}

	return true;
}

bool RtpPacketizer::MarkerBit(FrameType frame_type, int8_t payload_type)
{
	// Reference: rtp_sender_audio.cc:75
	// temporary code
	return false;
}

