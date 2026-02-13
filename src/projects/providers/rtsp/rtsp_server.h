#pragma once

#include "rtsp_library.h"
#include "rtsp_observer.h"
#include "rtsp_connection.h"
#include "rtp/rtp_track.h"
#include "rtp/rtp_udp_track.h"
#include "rtp/rtp_tcp_track.h"
#include "rtcp/rtcp_packet_header.h"

#include <map>
#include <base/base_traits.h>
#include <base/ovsocket/ovsocket.h>
#include <base/ovsocket/port_range.h>
#include <modules/physical_port/physical_port_manager.h>

#include <unordered_map>
#include <mutex>

class RtspServer : public ServerBase<RtspServer, ov::SocketType::Tcp>, public ObservableBase<RtspServer, RtspObserver>
{
public:
    /*
        Keeps stream id matched with track media type, codec, id, sdp parameters and clock frequency
    */
    struct StreamTrackInfo
    {
        uint32_t stream_id_;
        MediaTrack media_track_;
        std::shared_ptr<SdpFormatParameters> sdp_format_parameters_;
    };

    static constexpr char ClassName[] = "RtspServer";

private:
    struct RtspStreamTimestamp
    {
        uint32_t rtp_timestamp_ = 0;
        struct timeval ntp_timestamp_ = {};
        uint32_t offset_ = 0;
    };

    struct RtspStreamContext
    {
        uint32_t session_id_;
        size_t track_count_;
        std::vector<std::tuple<std::shared_ptr<RtspObserver>, info::application_id_t, uint32_t>> routes_;
        std::unordered_map<uint8_t, RtspStreamTimestamp> track_timestamps_;
        bool stream_offsets_initialized_ = false;
    };

public:
    virtual ~RtspServer() = default;

public:
    bool Disconnect(const ov::String &app_name, uint32_t stream_id);

    bool OnStreamAnnounced(const std::string_view &app_name, 
        const std::string_view &stream_name,
        const RtspMediaInfo &media_info);
    bool OnUdpStreamTrackSetup(const std::string_view &rtsp_uri,
        uint16_t rtp_client_port,
        uint16_t rtcp_client_port,
        uint32_t &session_id,
        RtpUdpTrack **track);
    bool OnTcpStreamTrackSetup(const std::string_view &rtsp_uri,
        uint16_t rtp_channel,
        uint16_t rtcp_channel,
        uint32_t &session_id,
        RtpTcpTrack **track);
    bool OnStreamTeardown(const std::string_view &app_name, 
        const std::string_view &stream_name);
    bool OnVideoData(uint32_t stream_id,
        uint8_t track_id,
        uint32_t timestamp,
        const std::shared_ptr<std::vector<uint8_t>> &data,
        uint8_t flags, 
        std::unique_ptr<FragmentationHeader> fragmentation_header);
    bool OnAudioData(uint32_t stream_id,
        uint8_t track_id,
        uint32_t timestamp,
        const std::shared_ptr<std::vector<uint8_t>> &data);
    void OnRtcpSenderReport(uint32_t stream_id,
        uint8_t track_id,
        const RtcpSenderReport &rtcp_sender_report);

protected:
    // PhysicalPortObserver
    void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;

    void OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
                        const ov::SocketAddress &address,
                        const std::shared_ptr<const ov::Data> &data) override;

    void OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
                        PhysicalPortDisconnectReason reason,
                        const std::shared_ptr<const ov::Error> &error) override;

private:
    std::vector<std::unique_ptr<RtpTrack>> *FindSessionTracks(const std::lock_guard<std::mutex> &lock, uint32_t stream_id);
    StreamTrackInfo *FindTrackStream(const std::lock_guard<std::mutex> &lock, const std::string &track_path);
 
    void DeleteStream(const std::lock_guard<std::mutex> &lock, std::unordered_map<std::string, uint32_t>::iterator &stream_id_iterator);

private:
    std::mutex mutex_;
    std::unordered_map<ov::Socket *, std::shared_ptr<RtspConnection>> requests_;
    // Maps stream path (e.g. /app/live to an internal id)
    std::unordered_map<std::string, uint32_t> stream_ids_;
    // Holds the context for each stream (routes, RTSP session id, ...)
    std::unordered_map<uint32_t, RtspStreamContext> stream_contexts_;
    // Holds the tracks of a given session via an RtpTrack abstraction
    std::unordered_map<uint32_t, std::vector<std::unique_ptr<RtpTrack>>> session_tracks_;
    // Maps track paths to stream id (e.g /app/live/audio_track -> 1),
    // this is needed since for UDP the control uri will be used to setup the track
    // and there is no other info to tie that setup request together with the actual stream from the setup request
    std::unordered_map<std::string, StreamTrackInfo> stream_tracks_;
    // TODO(rubu): make these configurable
    ov::PortRange<ov::SocketType::Udp> rtp_udp_port_range_ = {2000, 3000};
};
