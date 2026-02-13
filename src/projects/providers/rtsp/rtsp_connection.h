#pragma once

#include "rtsp_library.h"
#include "rtsp_request_consumer.h"
#include "rtsp_request_parser.h"
#include "rtsp_observer.h"
#include "rtsp.h"
#include "rtp/rtp_tcp_track.h"
#include "rtsp_rtp_demuxer.h"

#include <base/base_traits.h>
#include <base/ovsocket/ovsocket.h>

class RtspServer;

class RtspConnection : public RtspRequestConsumer
{
public:
    RtspConnection(RtspServer &rtsp_server, const std::shared_ptr<ov::Socket> &remote);
 
    void AppendRequestData(const std::shared_ptr<const ov::Data> &data);
    bool HasPendingData() const;

    // RtspRequestConsumer
    void OnRtspRequest(const RtspRequest &rtsp_request) override;

private:
    void SendResponse(const RtspRequest &rtsp_request, uint16_t status_code = 200);

private:
    RtspMethod last_rtsp_method_ = RtspMethod::Unknown;
    bool request_parser_has_pending_data_ = false;
    bool rtsp_rtp_demuxer_has_pending_data_ = false;
    bool has_interleaved_stream_data_ = false;
    RtspServer& rtsp_server_;
    std::shared_ptr<ov::Socket> remote_;
    RtspRequestParser parser_;
    RtspRtpDemuxer rtsp_rtp_demuxer_;
};