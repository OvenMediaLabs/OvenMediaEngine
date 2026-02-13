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
	std::shared_lock<std::shared_mutex> lock(_state_lock);
	if(GetNodeState() != ov::Node::NodeState::Ready)
	{
		logtt("It can only be called in the ready state.");
		return false;
	}

	_rtcp_sr_generators[ssrc] = std::make_shared<RtcpSRGenerator>(ssrc, codec_rate);

	if(_sdes == nullptr)
	{
		_sdes = std::make_shared<Sdes>();
	}

	_sdes->AddChunk(std::make_shared<SdesChunk>(ssrc, SdesChunk::Type::CNAME, cname));

	logtt("AddRtpSender : %d / %u / %u / %s", payload_type, ssrc, codec_rate, cname.CStr());

	return true;
}

bool RtpRtcp::AddRtpReceiver(const std::shared_ptr<MediaTrack> &track, const RtpTrackIdentifier &rtp_track_id)
{
	std::shared_lock<std::shared_mutex> lock(_state_lock);
	if(GetNodeState() != ov::Node::NodeState::Ready)
	{
		logtt("It can only be called in the ready state.");
		return false;
	}

	auto track_id = track->GetId();

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

	if (track->GetMediaType() == cmn::MediaType::Video)
	{
		_video_receiver_enabled = true;
	}
	else if (track->GetMediaType() == cmn::MediaType::Audio)
	{
		_audio_receiver_enabled = true;
	}

	_tracks[track_id] = track;
	_rtp_track_identifiers.push_back(rtp_track_id);
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
	std::shared_lock<std::shared_mutex> lock(_state_lock);
	// nothing to do before node start
	if(GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

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

		auto compound_rtcp_data = std::make_shared<ov::Data>(1024);
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
			_rtcp_send_stop_watch.Update();
			_rtcp_sdes = std::make_shared<RtcpPacket>();
			_rtcp_sdes->Build(_sdes);
		}

		compound_rtcp_data->Append(_rtcp_sdes->GetData());
		
		if(SendDataToNextNode(NodeType::Rtcp, compound_rtcp_data) == false)
		{
			logt("RTCP","Send RTCP failed : pt(%d) ssrc(%u)", rtp_packet->PayloadType(), rtp_packet->Ssrc());
		}
		else
		{
			logt("RTCP", "Send RTCP succeed : pt(%d) ssrc(%u) length(%d)", rtp_packet->PayloadType(), rtp_packet->Ssrc(), compound_rtcp_data->GetLength());
		}
	}

	// Send RTP
	_last_sent_rtp_packet = rtp_packet;
	return SendDataToNextNode(NodeType::Rtp, rtp_packet->GetData());
}

bool RtpRtcp::SendPLI(uint32_t track_id)
{
	auto stat_it = _receive_statistics.find(track_id);
	if(stat_it == _receive_statistics.end())
	{
		// Never received such SSRC packet
		return false;
	}

	auto stat = stat_it->second;
	
	auto pli = std::make_shared<PLI>();

	pli->SetSrcSsrc(stat->GetReceiverSSRC());
	pli->SetMediaSsrc(stat->GetMediaSSRC());

	auto rtcp_packet = std::make_shared<RtcpPacket>();
	rtcp_packet->Build(pli);

	_last_sent_rtcp_packet = rtcp_packet;

	return SendDataToNextNode(NodeType::Rtcp, rtcp_packet->GetData());
}

bool RtpRtcp::SendFIR(uint32_t track_id)
{
	auto stat_it = _receive_statistics.find(track_id);
	if(stat_it == _receive_statistics.end())
	{
		// Never received such SSRC packet
		return false;
	}

	auto stat = stat_it->second;
	
	auto fir = std::make_shared<FIR>();

	fir->SetSrcSsrc(stat->GetReceiverSSRC());
	fir->SetMediaSsrc(stat->GetMediaSSRC());
	fir->AddFirMessage(stat->GetMediaSSRC(), static_cast<uint8_t>(stat->GetNumberOfFirRequests()%256));
	auto rtcp_packet = std::make_shared<RtcpPacket>();
	rtcp_packet->Build(fir);

	stat->OnFirRequested();

	_last_sent_rtcp_packet = rtcp_packet;

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
	if (_ssrc_to_track_id.find(ssrc) != _ssrc_to_track_id.end())
	{	
		logtw("SSRC(%u) is already connected to track ID(%u), it will be replaced.", ssrc, _ssrc_to_track_id[ssrc]);
	}

	logti("Connect SSRC(%u) to track ID(%u)", ssrc, track_id);

	_ssrc_to_track_id[ssrc] = track_id;
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
		loge("RtpRtcp","Send data failed from(%d) data_len(%d)", static_cast<uint16_t>(from_node), data->GetLength());
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
	auto track_it = _tracks.find(track_id);
	if(track_it == _tracks.end())
	{
		logte("Could not find track info for track ID %u", track_id);
		return false;
	}
	auto track = track_it->second;

	// For RTCP Receiver Report
	std::shared_ptr<RtpReceiveStatistics> stat;
	auto stat_it = _receive_statistics.find(track_id);
	if(stat_it == _receive_statistics.end() || stat_it->second->GetMediaSSRC() != packet->Ssrc())
	{
		// First receive
		// Some encoders or servers do not provide SSRC in SDP. Therefore, after receiving the packet, the ssrc can be extracted and used.
		stat = std::make_shared<RtpReceiveStatistics>(packet->Ssrc(), track->GetTimeBase().GetDen());

		// If ssrc is changed, the previous statistics should be deleted.
		_receive_statistics[track_id] = stat;
	}
	else
	{
		stat = stat_it->second;
	}

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
			_last_sent_rtcp_packet = rtcp_packet;
			SendDataToNextNode(NodeType::Rtcp, rtcp_packet->GetData());
		}
	}

	// For Transport-wide CC feedback
	if (_transport_cc_feedback_enabled == true)
	{
		if (_transport_cc_generator == nullptr)
		{
			// Since the Receiver SSRC is unknown, the same as the RR of the first track is used. Since it is a wide sequence, media ssrc may not be one. So this also just uses the first media ssrc.
			_transport_cc_generator = std::make_shared<RtcpTransportCcFeedbackGenerator>(
										_transport_cc_feedback_extension_id, 
										stat->GetReceiverSSRC());
		}

		_transport_cc_generator->AddReceivedRtpPacket(packet);

		// Send Transport-wide CC feedback
		if ((_transport_cc_generator->HasElapsedSinceLastTransportCc(TRANSPORT_CC_CYCLE_MS)) && 
			(_video_receiver_enabled ? (track->GetMediaType() == cmn::MediaType::Video && packet->Marker() == true) : true))
		{
			auto feedback = _transport_cc_generator->GenerateTransportCcMessage();
			if (feedback != nullptr)
			{
				_last_sent_rtcp_packet = feedback;

				auto feedback_data = feedback->GetData();
				SendDataToNextNode(NodeType::Rtcp, feedback_data);
			}
		}
	}

	int jitter_buffer_type = 0;
	switch(track->GetOriginBitstream())
	{
		case cmn::BitstreamFormat::H264_RTP_RFC_6184:
		case cmn::BitstreamFormat::H265_RTP_RFC_7798:
		case cmn::BitstreamFormat::VP8_RTP_RFC_7741:
		case cmn::BitstreamFormat::AAC_MPEG4_GENERIC:
			jitter_buffer_type = 1;
			break;
		case cmn::BitstreamFormat::OPUS_RTP_RFC_7587:
			jitter_buffer_type = 2;
			break;
		default:
			break;
	}

	if(jitter_buffer_type == 1)
	{
		auto buffer_it = _rtp_frame_jitter_buffers.find(track_id);
		if(buffer_it == _rtp_frame_jitter_buffers.end())
		{
			// can not happen
			logte("Could not find jitter buffer for payload type %d", packet->PayloadType());
			return false;
		}

		auto jitter_buffer = buffer_it->second;

		jitter_buffer->InsertPacket(packet);

		auto frame = jitter_buffer->PopAvailableFrame();
		if(frame != nullptr && _observer != nullptr)
		{
			std::vector<std::shared_ptr<RtpPacket>> rtp_packets;

			auto packet = frame->GetFirstRtpPacket();
			if(packet == nullptr)
			{
				// can not happen
				logtw("Could not get first rtp packet from jitter buffer - track(%u)", track_id);
				return false;
			}

			rtp_packets.push_back(packet);

			while(true)
			{
				packet = frame->GetNextRtpPacket();
				if(packet == nullptr)
				{
					break;
				}

				rtp_packets.push_back(packet);
			}

			_observer->OnRtpFrameReceived(rtp_packets);
		}
	}
	else if(jitter_buffer_type == 2)
	{
		auto buffer_it = _rtp_minimal_jitter_buffers.find(track_id);
		if(buffer_it == _rtp_minimal_jitter_buffers.end())
		{
			// can not happen
			logte("Could not find jitter buffer for ssrc %u", packet->Ssrc());
			return false;
		}

		auto jitter_buffer = buffer_it->second;

		jitter_buffer->InsertPacket(packet);

		auto pop_packet = jitter_buffer->PopAvailablePacket();
		if(pop_packet != nullptr)
		{
			std::vector<std::shared_ptr<RtpPacket>> rtp_packets;
			rtp_packets.push_back(pop_packet);
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
				auto stat_it = _receive_statistics.find(track_id.value());
				if(stat_it != _receive_statistics.end())
				{
					auto stat = stat_it->second;
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
	return _last_sent_rtp_packet;
}

std::shared_ptr<RtcpPacket> RtpRtcp::GetLastSentRtcpPacket()
{
	return _last_sent_rtcp_packet;
}