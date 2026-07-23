#pragma once

#include "rtp_track.h"

#include <modules/physical_port/physical_port.h>
#include <modules/physical_port/physical_port_manager.h>
#include <base/ovsocket/port_range.h>

class RtspServer;

class RtpUdpTrack : public RtpTrack
{
    class ConnectionObserver : public PhysicalPortObserver
    {
    public:
        ConnectionObserver(RtpUdpTrack &track);

    protected:
        // Since these observers are UDP, we do not care for connected/disconnected events
        void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
        void OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
                            PhysicalPortDisconnectReason reason,
                            const std::shared_ptr<const ov::Error> &error) override;

    protected:
        RtpUdpTrack &track_;
    };

    class RtpConnectionObserver : public ConnectionObserver
    {
    public:
        using ConnectionObserver::ConnectionObserver;

    protected:
        void OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
                            const ov::SocketAddress &address,
                            const std::shared_ptr<const ov::Data> &data) override;
    };

    class RtcpConnectionObserver : public ConnectionObserver
    {
    public:
        using ConnectionObserver::ConnectionObserver;

    protected:
        void OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
                            const ov::SocketAddress &address,
                            const std::shared_ptr<const ov::Data> &data) override;
    };

public:
    RtpUdpTrack(RtspServer &rtsp_server,
        cmn::MediaType media_type,
        cmn::MediaCodecId media_codec_id,
        uint32_t stream_id,
        uint8_t track_id,
        uint32_t clock_frequency,
        std::shared_ptr<PhysicalPort> rtp_physical_port,
        std::shared_ptr<PhysicalPort> rtcp_physical_port);
    RtpUdpTrack(const RtpUdpTrack&) = delete;

public:
    void GetServerPorts(uint16_t &rtp_port, uint16_t &rtcp_port);

    template< typename U, ov::SocketType socket_type>
    static std::unique_ptr<U> Create(RtspServer &rtsp_server,
        cmn::MediaType media_type,
        cmn::MediaCodecId media_codec_id,
        uint32_t stream_id,
        uint8_t track_id,
        uint32_t clock_frequency,
        ov::PortRange<socket_type> &port_range,
        const char *host = "*")
    {
        std::shared_ptr<PhysicalPort> rtp_physical_port;
        std::shared_ptr<PhysicalPort> rtcp_physical_port;

        ov::SocketAddress address;
        address.SetHostname(host);
        for (const auto &port : port_range)
        {
            address.SetPort(port);
            auto physical_port = PhysicalPortManager::GetInstance()->CreatePort("RtpUdp", socket_type, address);
            if (physical_port)
            {
                if (rtp_physical_port == nullptr)
                {
                    rtp_physical_port = physical_port;
                    continue;
                }
                rtcp_physical_port = physical_port;
                port_range.SetNextAvailablePort(port + 1);
                break;
            }
        }

        if (rtp_physical_port && rtcp_physical_port)
        {
            return std::make_unique<U>(rtsp_server, media_type, media_codec_id, stream_id, track_id, clock_frequency, rtp_physical_port, rtcp_physical_port);
        }
        return nullptr;
    }

private:
    std::shared_ptr<PhysicalPort> rtp_physical_port_;
    std::shared_ptr<PhysicalPort> rtcp_physical_port_;
    RtpConnectionObserver rtp_observer_;
    RtcpConnectionObserver rtcp_observer_;
};