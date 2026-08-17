#define OV_LOG_TAG "RtspConnection"

#include "rtsp_connection.h"
#include "rtsp_stream.h"
#include "rtsp_server.h"
#include "rtsp.h"
#include "sdp.h"

#include <base/ovlibrary/stl.h>

RtspConnection::RtspConnection(RtspServer& rtsp_server, const std::shared_ptr<ov::Socket> &remote) : rtsp_server_(rtsp_server),
    remote_(remote),
    parser_(*this)
{
}

bool RtspConnection::HasPendingData() const
{
    return request_parser_has_pending_data_;
}

void RtspConnection::AppendRequestData(const std::shared_ptr<const ov::Data> &data)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t*>(data->GetData());
    size_t bytes_remaining = data->GetLength();
    while (bytes_remaining)
    {
        if (request_parser_has_pending_data_ == false
            && has_interleaved_stream_data_ == true 
            && last_rtsp_method_ == RtspMethod::Record)
        {
            while (bytes_remaining)
            {
                if (*bytes == '$' || rtsp_rtp_demuxer_has_pending_data_)
                {
                    size_t bytes_consumed = 0;
                    if (rtsp_rtp_demuxer_.AppendMuxedData(bytes, bytes_remaining, bytes_consumed, rtsp_rtp_demuxer_has_pending_data_) == false)
                    {
                        // We failed to demux the interleaved RTP the stream, shutdown the connection
                        remote_->Close();
                        return;
                    }
                    if (bytes_consumed > bytes_remaining)
                    {
                        logte("RTSP RTP demuxer consumed more bytes (%zu) than remaining in the packet (%zu)", bytes_consumed, bytes_remaining);
                        return;
                    }
                    bytes_remaining -= bytes_consumed;
                    bytes += bytes_consumed;
                }
                else
                {
                    break;
                }
            }
        }
        if (bytes_remaining)
        {
            // Currently just swallow everything we've got to parse the request
            // Normally this should not cause an issue since the interleaved stream data will begin only after our response (thus we will have drained the input buffer)
            // And as for the shutdown it should be the last command issued, so we can again safely drain everything
            // The question is what happens with GET_PARAMETER, if it is inlined
            // TODO(rubu): improve this
            request_parser_has_pending_data_ = parser_.AppendRequestData(std::vector<uint8_t>(bytes, bytes + bytes_remaining));
            bytes_remaining = 0;
        }
    }

}

void RtspConnection::OnRtspRequest(const RtspRequest &rtsp_request)
{
#if defined(DEBUG)
    {
        const auto &data = rtsp_request.GetData();
        logtt("Incoming RTSP request:\n%s", std::string(reinterpret_cast<const char*>(data.data()), data.size()).c_str());
        for (const auto& header : rtsp_request.GetHeaders())
        {
            logtt("%s: %s", std::string(header.first.data(), header.first.size()).c_str(), std::string(header.second.data(), header.second.size()).c_str());
        }
        const auto &body = rtsp_request.GetBody();
        if (body.empty() == false)
        {
            logtt("%s", std::string(reinterpret_cast<const char*>(body.data()), body.size()).c_str());
        }
    }
#endif
    const auto method = GetRtspMethod(rtsp_request.GetMethod());
    switch (method)
    {
    case RtspMethod::Options:
        {
            auto data = std::make_shared<ov::Data>();
            ov::ByteStream byte_stream(data.get());
            byte_stream << "RTSP/1.0 200 OK\r\n"
                "CSeq: " << std::to_string(rtsp_request.GetCSeq()) << "\r\n"
                "Public: GET_PARAMETER, ANNOUNCE, SETUP, RECORD, TEARDOWN\r\n\r\n";
            remote_->Send(data);
        }
        break;
    case RtspMethod::Announce:
        {
            std::string_view application_name, stream_name;
            if (ExtractApplicationAndStreamNames(rtsp_request.GetUri(), application_name, stream_name) == false)
            {
                // We can't split the uri into application name and stream components, give up and return 400;
                SendResponse(rtsp_request, 400);
                return;
            }
            RtspMediaInfo rtsp_media_info;
            const auto &sdp = rtsp_request.GetBody();
            ParseSdp(sdp, rtsp_media_info);
            bool success = rtsp_server_.OnStreamAnnounced(application_name, stream_name, rtsp_media_info);
            SendResponse(rtsp_request, success ? 200 : 500);
        }
        break;
    case RtspMethod::Setup:
        {
            auto transport_header = rtsp_request.GetHeader("Transport");
            if (transport_header.empty())
            {
                SendResponse(rtsp_request, 400);
                return;
            }
            auto transport_specification = Split(transport_header, ';');
            if (transport_specification.empty())
            {
                SendResponse(rtsp_request, 400);
                return;           
            }
            auto transport_specification_iterator = transport_specification.begin();
            auto transport_specification_components = Split(*transport_specification_iterator, '/');
            ov::SocketType socket_type = ov::SocketType::Udp;
            if (transport_specification_components.size() != 2 && transport_specification_components.size() != 3)
            {
                SendResponse(rtsp_request, 400);
                return;  
            }
            if (transport_specification_components.size() == 3)
            {
                if (transport_specification_components[2] == "TCP")
                {
                    socket_type = ov::SocketType::Tcp;
                    has_interleaved_stream_data_ = true;
                }
                else if (transport_specification_components[2] == "UDP")
                {
                    socket_type = ov::SocketType::Udp;
                }
                else
                {
                    SendResponse(rtsp_request, 400);
                    return;  
                }
            }
            std::string_view rtsp_path;
            if (ExtractPath(rtsp_request.GetUri(), rtsp_path) == false)
            {
                SendResponse(rtsp_request, 400);
                return;              
            }
            ++transport_specification_iterator;
            uint16_t client_rtp_port = 0, client_rtcp_port = 0, interleaved_rtp_channel = 0, interleaved_rtcp_channel = 0;
            while (transport_specification_iterator != transport_specification.end())
            {
                auto parameter = Split(*transport_specification_iterator, '=');
                if (parameter.empty() == false)
                {
                    if (parameter[0] == "client_port" && parameter.size() > 1)
                    {
                        auto client_ports = Split(parameter[1], '-');
                        Stoi(std::string(client_ports[0].data(), client_ports[0].size()), client_rtp_port);
                        Stoi(std::string(client_ports[1].data(), client_ports[1].size()), client_rtcp_port);
                    }
                    else if (parameter[0] == "interleaved" && parameter.size() > 1)
                    {
                        auto interleaved_channels = Split(parameter[1], '-');
                        Stoi(std::string(interleaved_channels[0].data(), interleaved_channels[0].size()), interleaved_rtp_channel);
                        Stoi(std::string(interleaved_channels[1].data(), interleaved_channels[1].size()), interleaved_rtcp_channel);
                    }
                }
                ++transport_specification_iterator;
            }
            uint32_t session_id = 0;
            if (socket_type == ov::SocketType::Udp)
            {
                uint16_t server_rtp_port = 0, server_rtcp_port = 0;
                RtpUdpTrack *rtp_udp_track = nullptr;
                rtsp_server_.OnUdpStreamTrackSetup(rtsp_path, 
                    client_rtp_port,
                    client_rtcp_port,
                    session_id,
                    &rtp_udp_track);
                if (rtp_udp_track == nullptr)
                {
                    SendResponse(rtsp_request, 500);
                    return;                 
                }
                rtp_udp_track->GetServerPorts(server_rtp_port, server_rtcp_port);
                auto data = std::make_shared<ov::Data>();
                ov::ByteStream byte_stream(data.get());
                byte_stream << "RTSP/1.0 200 OK\r\nCSeq: " << std::to_string(rtsp_request.GetCSeq()) << "\r\n"
                    "Transport: " <<  transport_header << ";server_port=" << std::to_string(server_rtp_port) << "-" << std::to_string(server_rtcp_port) << "\r\n"
                    "Session: " << std::to_string(session_id) << "\r\n\r\n";
                remote_->Send(data);
            }
            else
            {
                RtpTcpTrack *rtp_tcp_track = nullptr;
                rtsp_server_.OnTcpStreamTrackSetup(rtsp_path, 
                    interleaved_rtp_channel,
                    interleaved_rtcp_channel,
                    session_id,
                    &rtp_tcp_track);
                if (rtp_tcp_track == nullptr)
                {
                    SendResponse(rtsp_request, 500);
                    return;                 
                }
                auto data = std::make_shared<ov::Data>();
                ov::ByteStream byte_stream(data.get());
                byte_stream << "RTSP/1.0 200 OK\r\nCSeq: " << std::to_string(rtsp_request.GetCSeq()) << "\r\n"
                    "Transport: " <<  transport_header << "\r\n"
                    "Session: " << std::to_string(session_id) << "\r\n\r\n";
                remote_->Send(data);
                rtsp_rtp_demuxer_.AddInterleavedTrack(interleaved_rtp_channel, rtp_tcp_track);
                rtsp_rtp_demuxer_.AddInterleavedTrack(interleaved_rtcp_channel, rtp_tcp_track);
            }
        }
        break;
    case RtspMethod::Record:
        SendResponse(rtsp_request);
        break;
    case RtspMethod::Teardown:
        {
            std::string_view application_name, stream_name;
            if (ExtractApplicationAndStreamNames(rtsp_request.GetUri(), application_name, stream_name) == false)
            {
                // We can't split the uri into application name and stream components, give up and return 400;
                SendResponse(rtsp_request, 400);
                return;
            }
            rtsp_server_.OnStreamTeardown(application_name, stream_name);
            SendResponse(rtsp_request, 200);
        }
    case RtspMethod::Unknown:
    case RtspMethod::GetParameter:
    default:
        break;
    }
    last_rtsp_method_ = method;
}

void RtspConnection::SendResponse(const RtspRequest &rtsp_request, uint16_t status_code)
{
    auto data = std::make_shared<ov::Data>();
    ov::ByteStream byte_stream(data.get());
    std::string message;
    switch (status_code)
    {
    case 200:
        message = "OK";
        break;
    case 400:
        message = "Bad Request";
        break;
    case 500:
        message = "Internal Server Error";
        break;
    default:
        OV_ASSERT2(false);
        break;
    }
    byte_stream << "RTSP/1.0 " << std::to_string(status_code) << " " << message << "\r\n"
        "CSeq: " << std::to_string(rtsp_request.GetCSeq()) << "\r\n\r\n";
    remote_->Send(data);
  
}