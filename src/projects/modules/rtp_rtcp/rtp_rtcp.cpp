#include <base/ovlibrary/byte_io.h>

#include "rtp_rtcp.h"
#include "publishers/webrtc/rtc_application.h"
#include "publishers/webrtc/rtc_stream.h"
#include "rtcp_receiver.h"
#include "rtcp_info/fir.h"
#include "rtcp_info/pli.h"

#include "modules/rtsp/rtsp_data.h"

#define OV_LOG_TAG "RtpRtcp"

RtpRtcp::RtpRtcp(const std::shared_ptr<RtpRtcpInterface> &observer)
	        : ov::Node(NodeType::Rtp)
{
	_observer = observer;
	_rtcp_send_stop_watch.Start();
}

RtpRtcp::~RtpRtcp()
{
    _rtcp_sr_generators.clear();
}

bool RtpRtcp::AddRtpSender(uint8_t payload_type, uint32_t ssrc, uint32_t codec_rate, ov::String cname)
{
	std::shared_lock<std::shared_mutex> state_lock(_state_lock);
	if(GetNodeState() != ov::Node::NodeState::Ready)
	{
		logtt("It can only be called in the ready state.");
		return false;
	}

	auto sr_generator = std::make_shared<RtcpSRGenerator>(ssrc, codec_rate);
	auto sdes_chunk = std::make_shared<SdesChunk>(ssrc, SdesChunk::Type::CNAME, cname);

	{
		std::lock_guard<std::shared_mutex> lock(_rtcp_send_state_lock);
		_rtcp_sr_generators[ssrc] = sr_generator;

		if(_sdes == nullptr)
		{
			_sdes = std::make_shared<Sdes>();
		}
		_sdes->AddChunk(sdes_chunk);
	}

	logtt("AddRtpSender : %d / %u / %u / %s", payload_type, ssrc, codec_rate, cname.CStr());

	return true;
}

bool RtpRtcp::AddRtpReceiver(const std::shared_ptr<MediaTrack> &track, const RtpTrackIdentifier &rtp_track_id)
{
	std::shared_lock<std::shared_mutex> state_lock(_state_lock);
	if(GetNodeState() != ov::Node::NodeState::Ready)
	{
		logtt("It can only be called in the ready state.");
		return false;
	}

	auto track_id = track->GetId();

	{
		std::lock_guard<std::shared_mutex> lock(_track_info_lock);
		switch(track->GetOriginBitstream())
		{
			case cmn::BitstreamFormat::H264_RTP_RFC_6184:
			case cmn::BitstreamFormat::H265_RTP_RFC_7798:
			case cmn::BitstreamFormat::VP8_RTP_RFC_7741:
			case cmn::BitstreamFormat::AAC_MPEG4_GENERIC:
				_rtp_frame_jitter_buffers[track_id] = std::make_shared<RtpFrameJitterBuffer>();
				break;
			case cmn::BitstreamFormat::OPUS_RTP_RFC_7587:
				_rtp_minimal_jitter_buffers[track_id] = std::make_shared<RtpMinimalJitterBuffer>();
				break;
			default:
				logte("RTP Receiver cannot support %d input stream format", static_cast<int8_t>(track->GetOriginBitstream()));
				return false;
		}

		_tracks[track_id] = track;
		_rtp_track_identifiers.push_back(rtp_track_id);
	}

	if (track->GetMediaType() == cmn::MediaType::Video)
	{
		_video_receiver_enabled = true;
	}
	else if (track->GetMediaType() == cmn::MediaType::Audio)
	{
		_audio_receiver_enabled = true;
	}

	if (rtp_track_id.ssrc.has_value())
	{
		logti("AddRtpReceiver : %d / %u / %s / %s", track_id, rtp_track_id.ssrc.value(), rtp_track_id.mid.value_or(ov::String("")).CStr(), rtp_track_id.rid.value_or(ov::String("")).CStr());
		ConnectSsrcToTrack(rtp_track_id.ssrc.value(), track_id);
	}

	return true;
}

bool RtpRtcp::Stop()
{
	// Cross reference
	std::lock_guard<std::shared_mutex> lock(_state_lock);
	_observer.reset();

	return Node::Stop();
}

bool RtpRtcp::SendRtpPacket(const std::shared_ptr<RtpPacket> &rtp_packet)
{
	std::shared_lock<std::shared_mutex> state_lock(_state_lock);
	// nothing to do before node start
	if(GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

	// Build the compound RTCP under the send lock, then send outside the lock
	std::shared_ptr<ov::Data> compound_rtcp_data = nullptr;
	{
		std::lock_guard<std::shared_mutex> lock(_rtcp_send_state_lock);

		// RTCP(SR + SR + SDES + SDES)
		auto it = _rtcp_sr_generators.find(rtp_packet->Ssrc());
		if(it != _rtcp_sr_generators.end())
		{
			auto rtcp_sr_generator = it->second;
			rtcp_sr_generator->AddRTPPacketInfo(rtp_packet);
		}

		if(_rtcp_sent_count == 0 || _rtcp_send_stop_watch.Elapsed() > SDES_CYCLE_MS)
		{
			_rtcp_send_stop_watch.Update();
			_rtcp_sent_count ++;

			compound_rtcp_data = std::make_shared<ov::Data>(1024);
			for(const auto &item : _rtcp_sr_generators)
			{
				auto rtcp_sr_generator = item.second;
				auto rtcp_sr_packet = rtcp_sr_generator->PopRtcpSRPacket();
				if(rtcp_sr_packet == nullptr)
				{
					continue;
				}
				compound_rtcp_data->Append(rtcp_sr_packet->GetData());
			}

			if(_rtcp_sdes == nullptr)
			{
				_rtcp_sdes = std::make_shared<RtcpPacket>();
				_rtcp_sdes->Build(_sdes);
			}

			compound_rtcp_data->Append(_rtcp_sdes->GetData());
		}
	}

	if(compound_rtcp_data != nullptr)
	{
		if(SendDataToNextNode(NodeType::Rtcp, compound_rtcp_data) == false)
		{
			logt("RTCP","Send RTCP failed : pt(%d) ssrc(%u)", rtp_packet->PayloadType(), rtp_packet->Ssrc());
		}
		else
		{
			logt("RTCP", "Send RTCP succeed : pt(%d) ssrc(%u) length(%zu)", rtp_packet->PayloadType(), rtp_packet->Ssrc(), compound_rtcp_data->GetLength());
		}
	}

	// Send RTP
	SetLastSentRtpPacket(rtp_packet);
	return SendDataToNextNode(NodeType::Rtp, rtp_packet->GetData());
}

bool RtpRtcp::SendPLI(uint32_t track_id)
{
	auto stat = FindReceiveStatistics(track_id);
	if(stat == nullptr)
	{
		// Never received such SSRC packet
		return false;
	}

	auto pli = std::make_shared<PLI>();

	pli->SetSrcSsrc(stat->GetReceiverSSRC());
	pli->SetMediaSsrc(stat->GetMediaSSRC());

	auto rtcp_packet = std::make_shared<RtcpPacket>();
	rtcp_packet->Build(pli);

	SetLastSentRtcpPacket(rtcp_packet);

	return SendDataToNextNode(NodeType::Rtcp, rtcp_packet->GetData());
}

bool RtpRtcp::SendFIR(uint32_t track_id)
{
	auto stat = FindReceiveStatistics(track_id);
	if(stat == nullptr)
	{
		// Never received such SSRC packet
		return false;
	}

	auto fir = std::make_shared<FIR>();

	fir->SetSrcSsrc(stat->GetReceiverSSRC());
	fir->SetMediaSsrc(stat->GetMediaSSRC());
	fir->AddFirMessage(stat->GetMediaSSRC(), static_cast<uint8_t>(stat->GetNumberOfFirRequests()%256));
	auto rtcp_packet = std::make_shared<RtcpPacket>();
	rtcp_packet->Build(fir);

	stat->OnFirRequested();

	SetLastSentRtcpPacket(rtcp_packet);

	return SendDataToNextNode(NodeType::Rtcp, rtcp_packet->GetData());
}

bool RtpRtcp::IsTransportCcFeedbackEnabled() const
{
	return _transport_cc_feedback_enabled;
}

bool RtpRtcp::EnableTransportCcFeedback(uint8_t extension_id)
{
	_transport_cc_feedback_extension_id = extension_id;
	_transport_cc_feedback_enabled = true;

	return true;
}

void RtpRtcp::DisableTransportCcFeedback()
{
	_transport_cc_feedback_enabled = false;
}

std::optional<uint32_t> RtpRtcp::GetTrackId(uint32_t ssrc) const
{
	std::shared_lock<std::shared_mutex> lock(_ssrc_to_track_id_lock);
	auto it = _ssrc_to_track_id.find(ssrc);
	if(it == _ssrc_to_track_id.end())
	{
		return std::nullopt;
	}

	return it->second;
}

// Find track id by mid or mid + rid
std::optional<uint32_t> RtpRtcp::FindTrackId(const std::shared_ptr<const RtpPacket> &rtp_packet) const
{
	auto track_id = GetTrackId(rtp_packet->Ssrc());
	if(track_id.has_value())
	{
		return track_id;
	}

	std::shared_lock<std::shared_mutex> lock(_track_info_lock);
	for (const auto &rtp_track_id : _rtp_track_identifiers)
	{
		// with ssrc
		if (rtp_track_id.ssrc.has_value() && rtp_track_id.ssrc.value() == rtp_packet->Ssrc())
		{
			return rtp_track_id.GetTrackId();
		}

		// Get mid from rtp header extension
		auto mid = rtp_packet->GetExtension(rtp_track_id.mid_extension_id);
		auto rid = rtp_packet->GetExtension(rtp_track_id.rid_extension_id);

		// with mid or mid + rid
		if (rtp_track_id.mid.has_value() && mid.has_value() &&
			mid.value().ToString() == rtp_track_id.mid.value())
		{
			// mid + rid
			if (rtp_track_id.rid.has_value() && rid.has_value() &&
				rid.value().ToString() == rtp_track_id.rid.value())
			{
				return rtp_track_id.GetTrackId();
			}
			// mid only
			else if (rtp_track_id.rid.has_value() == false)
			{
				return rtp_track_id.GetTrackId();
			}
		}
	}

	return std::nullopt;
}

// Find track id by cname or cname + rid
std::optional<uint32_t> RtpRtcp::FindTrackId(const std::shared_ptr<const Sdes> &sdes) const
{
	auto track_id = GetTrackId(sdes->GetRtpSsrc());
	if(track_id.has_value())
	{
		return track_id;
	}

	std::shared_lock<std::shared_mutex> lock(_track_info_lock);
	for (const auto &rtp_track_id : _rtp_track_identifiers)
	{
		// with ssrc
		if (rtp_track_id.ssrc.has_value() && rtp_track_id.ssrc.value() == sdes->GetRtpSsrc())
		{
			return rtp_track_id.GetTrackId();
		}

		// with cname or cname + rid
		if (rtp_track_id.cname.has_value())
		{
			auto sdes_chunk = sdes->GetChunk(SdesChunk::Type::CNAME);
			if (sdes_chunk != nullptr && sdes_chunk->GetText() == rtp_track_id.cname.value())
			{
				// cname + rid
				if (rtp_track_id.rid.has_value())
				{
					auto rid_chunk = sdes->GetChunk(SdesChunk::Type::RtpStreamId);
					if (rid_chunk != nullptr && rid_chunk->GetText() == rtp_track_id.rid.value())
					{
						return rtp_track_id.GetTrackId();
					}
				}
				// cname only
				else
				{
					return rtp_track_id.GetTrackId();
				}
			}
		}
	}

	return std::nullopt;
}

std::optional<uint32_t> RtpRtcp::FindTrackId(uint8_t rtsp_inter_channel) const
{
	std::shared_lock<std::shared_mutex> lock(_track_info_lock);
	for (const auto &rtp_track_id : _rtp_track_identifiers)
	{
		// with interleaved channel
		if (rtp_track_id.interleaved_channel.has_value() && rtp_track_id.interleaved_channel.value() == rtsp_inter_channel)
		{
			return rtp_track_id.GetTrackId();
		}
	}

	return std::nullopt;
}

void RtpRtcp::ConnectSsrcToTrack(uint32_t ssrc, uint32_t track_id)
{
	{
		std::lock_guard<std::shared_mutex> lock(_ssrc_to_track_id_lock);
		if (_ssrc_to_track_id.find(ssrc) != _ssrc_to_track_id.end())
		{
			logtw("SSRC(%u) is already connected to track ID(%u), it will be replaced.", ssrc, _ssrc_to_track_id[ssrc]);
		}
		_ssrc_to_track_id[ssrc] = track_id;
	}

	logti("Connect SSRC(%u) to track ID(%u)", ssrc, track_id);
}

std::shared_ptr<MediaTrack> RtpRtcp::GetTrack(uint32_t track_id) const
{
	std::shared_lock<std::shared_mutex> lock(_track_info_lock);
	auto it = _tracks.find(track_id);
	if (it == _tracks.end())
	{
		return nullptr;
	}
	return it->second;
}

std::shared_ptr<RtpFrameJitterBuffer> RtpRtcp::GetJitterBuffer(uint32_t track_id)
{
	std::shared_lock<std::shared_mutex> lock(_track_info_lock);
	auto it = _rtp_frame_jitter_buffers.find(track_id);
	if (it == _rtp_frame_jitter_buffers.end())
	{
		return nullptr;
	}
	return it->second;
}

std::shared_ptr<RtpMinimalJitterBuffer> RtpRtcp::GetMinimalJitterBuffer(uint32_t track_id)
{
	std::shared_lock<std::shared_mutex> lock(_track_info_lock);
	auto it = _rtp_minimal_jitter_buffers.find(track_id);
	if (it == _rtp_minimal_jitter_buffers.end())
	{
		return nullptr;
	}
	return it->second;
}

std::shared_ptr<RtpReceiveStatistics> RtpRtcp::GetOrCreateReceiveStatistics(uint32_t track_id, uint32_t ssrc, uint32_t clock_rate)
{
	std::lock_guard<std::shared_mutex> lock(_receive_statistics_lock);
	auto it = _receive_statistics.find(track_id);
	// Some encoders or servers do not provide SSRC in SDP, so it is extracted from
	// the received packet. If the ssrc changes, the previous statistics are replaced.
	if (it == _receive_statistics.end() || it->second->GetMediaSSRC() != ssrc)
	{
		auto stat = std::make_shared<RtpReceiveStatistics>(ssrc, clock_rate);
		_receive_statistics[track_id] = stat;
		return stat;
	}
	return it->second;
}

std::shared_ptr<RtpReceiveStatistics> RtpRtcp::FindReceiveStatistics(uint32_t track_id) const
{
	std::shared_lock<std::shared_mutex> lock(_receive_statistics_lock);
	auto it = _receive_statistics.find(track_id);
	if (it == _receive_statistics.end())
	{
		return nullptr;
	}
	return it->second;
}

std::shared_ptr<RtcpPacket> RtpRtcp::GenerateTransportCcFeedbackIfNeeded(const std::shared_ptr<RtpPacket> &packet, uint32_t receiver_ssrc, bool is_video, bool marker)
{
	std::shared_ptr<RtcpTransportCcFeedbackGenerator> generator;
	{
		std::lock_guard<std::shared_mutex> lock(_transport_cc_generator_lock);
		if (_transport_cc_generator == nullptr)
		{
			// Receiver SSRC is unknown, so reuse the first track's RR ssrc. Wide
			// sequence means media ssrc may not be unique, so the first one is used.
			_transport_cc_generator = std::make_shared<RtcpTransportCcFeedbackGenerator>(_transport_cc_feedback_extension_id.load(), receiver_ssrc);
		}
		generator = _transport_cc_generator;
	}

	generator->AddReceivedRtpPacket(packet);

	if (generator->HasElapsedSinceLastTransportCc(TRANSPORT_CC_CYCLE_MS) &&
		(_video_receiver_enabled ? (is_video && marker) : true))
	{
		return generator->GenerateTransportCcMessage();
	}
	return nullptr;
}

void RtpRtcp::SetLastSentRtpPacket(const std::shared_ptr<RtpPacket> &packet)
{
	std::lock_guard<std::shared_mutex> lock(_last_sent_packet_lock);
	_last_sent_rtp_packet = packet;
}

void RtpRtcp::SetLastSentRtcpPacket(const std::shared_ptr<RtcpPacket> &packet)
{
	std::lock_guard<std::shared_mutex> lock(_last_sent_packet_lock);
	_last_sent_rtcp_packet = packet;
}

// In general, since RTP_RTCP is the first node, there is no previous node. So it will not be called
bool RtpRtcp::OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data)
{
	std::shared_lock<std::shared_mutex> lock(_state_lock);
	// nothing to do before node start
	if(GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

	if(SendDataToNextNode(from_node, data) == false)
	{
		loge("RtpRtcp","Send data failed from(%d) data_len(%zu)", static_cast<uint16_t>(from_node), data->GetLength());
		return false;
	}

	return true;
}

// Implement Node Interface
// decoded data from srtp
// no upper node( receive data process end)
bool RtpRtcp::OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data)
{
	// In the case of UDP, one complete packet is received here.
	// In the case of TCP, demuxing is already performed in the lower layer 
	// such as IcePort or RTSP Interleaved channel to complete and input one packet.
	// Therefore, it is not necessary to demux the packet here.

	std::shared_lock<std::shared_mutex> lock(_state_lock);
	// nothing to do before node start
	if(GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

	// std::min(FIXED_HEADER_SIZE, RTCP_HEADER_SIZE)
	if(data->GetLength() < RTCP_HEADER_SIZE)
	{
		logtt("It is not an RTP or RTCP packet.");
		return false;
	}

	/* Check if this is a RTP/RTCP packet
		https://www.rfc-editor.org/rfc/rfc7983.html
					+----------------+
					|        [0..3] -+--> forward to STUN
					|                |
					|      [16..19] -+--> forward to ZRTP
					|                |
		packet -->  |      [20..63] -+--> forward to DTLS
					|                |
					|      [64..79] -+--> forward to TURN Channel
					|                |
					|    [128..191] -+--> forward to RTP/RTCP
					+----------------+
	*/
	auto first_byte = data->GetDataAs<uint8_t>()[0];
	if(first_byte >= 128 && first_byte <= 191)
	{
		// Distinguish between RTP and RTCP 
		// https://tools.ietf.org/html/rfc5761#section-4
		auto payload_type = data->GetDataAs<uint8_t>()[1];
		// RTCP
		if(payload_type >= 192 && payload_type <= 223)
		{
			return OnRtcpReceived(from_node, data);
		}
		// RTP
		else
		{
			return OnRtpReceived(from_node, data);
		}
	}
	else
	{
		logtt("It is not an RTP or RTCP packet.");
		return false;
	}

    return true;
}

bool RtpRtcp::OnRtpReceived(NodeType from_node, const std::shared_ptr<const ov::Data> &data)
{
	auto packet = std::make_shared<RtpPacket>(data);

	std::optional<uint32_t> track_id_opt = GetTrackId(packet->Ssrc());
	if (track_id_opt.has_value() == false)
	{
		if(from_node == NodeType::Rtsp)
		{
			auto rtsp_data = std::static_pointer_cast<const RtspData>(data);
			if(rtsp_data == nullptr)
			{
				logte("Could not convert to RtspData");
				return false;
			}

			// RTSP Node uses channelID as trackID
			track_id_opt = FindTrackId(rtsp_data->GetChannelId());
			if (track_id_opt.has_value() == false)
			{
				logte("Could not find track ID for RTSP channel ID %u", rtsp_data->GetChannelId());
				return false;
			}
		}
		else
		{
			track_id_opt = FindTrackId(packet);
			if (track_id_opt.has_value() == false)
			{
				logte("Could not find track ID for SSRC %u", packet->Ssrc());
				return false;
			}
		}

		ConnectSsrcToTrack(packet->Ssrc(), track_id_opt.value());
	}

	if (from_node == NodeType::Rtsp)
	{
		// RTSP Node uses channelID as trackID
		packet->SetRtspChannel(track_id_opt.value());
	}

	auto track_id = track_id_opt.value();
	auto track = GetTrack(track_id);
	if(track == nullptr)
	{
		logte("Could not find track info for track ID %u", track_id);
		return false;
	}

	// For RTCP Receiver Report
	auto stat = GetOrCreateReceiveStatistics(track_id, packet->Ssrc(), track->GetTimeBase().GetDen());

	stat->AddReceivedRtpPacket(packet);

	// Send ReceiverReport
	if (stat->HasElapsedSinceLastReportBlock(RECEIVER_REPORT_CYCLE_MS) && stat->IsSenderReportReceived() == true)
	{
		auto report = std::make_shared<ReceiverReport>();
		report->SetRtpSsrc(packet->Ssrc());
		report->SetSenderSsrc(stat->GetReceiverSSRC());
		report->AddReportBlock(stat->GenerateReportBlock());

		auto rtcp_packet = std::make_shared<RtcpPacket>();
		if(rtcp_packet->Build(report) == true)
		{
			SetLastSentRtcpPacket(rtcp_packet);
			SendDataToNextNode(NodeType::Rtcp, rtcp_packet->GetData());
		}
	}

	// For Transport-wide CC feedback
	if (_transport_cc_feedback_enabled == true)
	{
		auto feedback = GenerateTransportCcFeedbackIfNeeded(packet, stat->GetReceiverSSRC(),
														   track->GetMediaType() == cmn::MediaType::Video, packet->Marker() == true);
		if (feedback != nullptr)
		{
			SetLastSentRtcpPacket(feedback);
			SendDataToNextNode(NodeType::Rtcp, feedback->GetData());
		}
	}

	// Frame: H264/H265/VP8/AAC reassemble into a frame. Minimal: Opus passes per-packet.
	enum class JitterBufferKind { None, Frame, Minimal };
	JitterBufferKind jitter_buffer_kind = JitterBufferKind::None;
	switch(track->GetOriginBitstream())
	{
		case cmn::BitstreamFormat::H264_RTP_RFC_6184:
		case cmn::BitstreamFormat::H265_RTP_RFC_7798:
		case cmn::BitstreamFormat::VP8_RTP_RFC_7741:
		case cmn::BitstreamFormat::AAC_MPEG4_GENERIC:
			jitter_buffer_kind = JitterBufferKind::Frame;
			break;
		case cmn::BitstreamFormat::OPUS_RTP_RFC_7587:
			jitter_buffer_kind = JitterBufferKind::Minimal;
			break;
		default:
			break;
	}

	// Lambda : Push RTP packet to the list if it is not a padding-only packet.
	auto PushPacketIfNeed = [&](std::vector<std::shared_ptr<RtpPacket>> &rtp_packets, const std::shared_ptr<RtpPacket> &rtp_packet) {
		if (rtp_packet == nullptr)
		{
			return;
		}

		// Drop Padding-only RTP packet
		if (rtp_packet->HasPadding() && rtp_packet->PayloadSize() == 0)
		{
			logtp("Drop padding-only RTP packet - track(%u) | %s", track_id, rtp_packet->Dump().CStr());
			return;
		}

		rtp_packets.push_back(rtp_packet);
	};

	if(jitter_buffer_kind == JitterBufferKind::Frame)
	{
		auto jitter_buffer = GetJitterBuffer(track_id);
		if(jitter_buffer == nullptr)
		{
			// can not happen
			logte("Could not find jitter buffer for payload type %d", packet->PayloadType());
			return false;
		}

		jitter_buffer->InsertPacket(packet);

		auto frame = jitter_buffer->PopAvailableFrame();
		if (frame != nullptr && _observer != nullptr)
		{
			std::vector<std::shared_ptr<RtpPacket>> rtp_packets;

			auto first_packet = frame->GetFirstRtpPacket();
			if (first_packet == nullptr)
			{
				// can not happen
				logtw("Could not get first rtp packet from jitter buffer - track(%u)", track_id);
				return false;
			}

			PushPacketIfNeed(rtp_packets, first_packet);

			while (true)
			{
				auto next_packet = frame->GetNextRtpPacket();
				if (next_packet == nullptr)
				{
					break;
				}

				PushPacketIfNeed(rtp_packets, next_packet);
			}

			if (rtp_packets.empty())
			{
				return true;
			}

			_observer->OnRtpFrameReceived(rtp_packets);
		}
	}
	else if (jitter_buffer_kind == JitterBufferKind::Minimal)
	{
		auto jitter_buffer = GetMinimalJitterBuffer(track_id);
		if (jitter_buffer == nullptr)
		{
			// can not happen
			logte("Could not find jitter buffer for ssrc %u", packet->Ssrc());
			return false;
		}

		jitter_buffer->InsertPacket(packet);

		auto pop_packet = jitter_buffer->PopAvailablePacket();
		if (pop_packet != nullptr)
		{
			std::vector<std::shared_ptr<RtpPacket>> rtp_packets;

			PushPacketIfNeed(rtp_packets, pop_packet);

			if (rtp_packets.empty())
			{
				return true;
			}

			_observer->OnRtpFrameReceived(rtp_packets);
		}
	}
	else
	{
		logte("Could not find jitter buffer for payload type %d", packet->PayloadType());
	}

	return true;
}

bool RtpRtcp::OnRtcpReceived(NodeType from_node, const std::shared_ptr<const ov::Data> &data)
{
	// Parse RTCP Packet
	RtcpReceiver receiver;
	if(receiver.ParseCompoundPacket(data) == false)
	{
		return false;
	}

	uint32_t rtsp_channel = 0;
	if(from_node == NodeType::Rtsp)
	{
		auto rtsp_data = std::static_pointer_cast<const RtspData>(data);
		if(rtsp_data == nullptr)
		{
			logte("Could not convert to RtspData");
			return false;
		}

		// RTSP Node uses channelID as trackID
		rtsp_channel = rtsp_data->GetChannelId();
	}

	while(receiver.HasAvailableRtcpInfo())
	{
		auto info = receiver.PopRtcpInfo();
		info->SetRtspChannel(rtsp_channel);

		if(info->GetPacketType() == RtcpPacketType::SR)
		{
			auto sr = std::dynamic_pointer_cast<SenderReport>(info);
			auto track_id = GetTrackId(sr->GetSenderSsrc());
			if (track_id.has_value())
			{
				auto stat = FindReceiveStatistics(track_id.value());
				if(stat != nullptr)
				{
					stat->AddReceivedRtcpSenderReport(sr);
				}
			}
		}
		
		if(_observer != nullptr)
		{
			_observer->OnRtcpReceived(info);
		}
	}
	
	return true;
}

std::shared_ptr<RtpPacket> RtpRtcp::GetLastSentRtpPacket()
{
	std::shared_lock<std::shared_mutex> lock(_last_sent_packet_lock);
	return _last_sent_rtp_packet;
}

std::shared_ptr<RtcpPacket> RtpRtcp::GetLastSentRtcpPacket()
{
	std::shared_lock<std::shared_mutex> lock(_last_sent_packet_lock);
	return _last_sent_rtcp_packet;
}