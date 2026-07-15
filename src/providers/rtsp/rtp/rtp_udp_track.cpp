#include "rtp_udp_track.h"

#include <modules/physical_port/physical_port_manager.h>

RtpUdpTrack::ConnectionObserver::ConnectionObserver(RtpUdpTrack &track) : track_(track)
{
}

void RtpUdpTrack::ConnectionObserver::OnConnected(const std::shared_ptr<ov::Socket> &remote)
{
}

void RtpUdpTrack::ConnectionObserver::OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
    PhysicalPortDisconnectReason reason,
    const std::shared_ptr<const ov::Error> &error)
{
}

void RtpUdpTrack::RtpConnectionObserver::OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
    const ov::SocketAddress &address,
    const std::shared_ptr<const ov::Data> &data)

{
    // TODO(rubu): Add jitter buffer for UDP packet reordering
    const auto *rtp_packet = data->GetDataAs<uint8_t>();
    track_.AddRtpPacket(std::make_shared<std::vector<uint8_t>>(rtp_packet, rtp_packet + data->GetLength()));
}

void RtpUdpTrack::RtcpConnectionObserver::OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
    const ov::SocketAddress &address,
    const std::shared_ptr<const ov::Data> &data)
{
}

void RtpUdpTrack::GetServerPorts(uint16_t &rtp_port, uint16_t &rtcp_port)
{
    rtp_port = rtp_physical_port_->GetAddress().Port();
    rtcp_port = rtcp_physical_port_->GetAddress().Port();
}

RtpUdpTrack::RtpUdpTrack(RtspServer &rtsp_server,
    cmn::MediaType media_type,
    cmn::MediaCodecId media_codec_id,
    uint32_t stream_id,
    uint8_t track_id,
    uint32_t clock_frequency,
    std::shared_ptr<PhysicalPort> rtp_physical_port,
    std::shared_ptr<PhysicalPort> rtcp_physical_port) : RtpTrack(rtsp_server, media_type, media_codec_id, stream_id, track_id, clock_frequency),
    rtp_physical_port_(rtp_physical_port),
    rtcp_physical_port_(rtcp_physical_port),
    rtp_observer_(*this),
    rtcp_observer_(*this)
{
    rtp_physical_port_->AddObserver(&rtp_observer_);
    rtcp_physical_port_->AddObserver(&rtcp_observer_);
}

