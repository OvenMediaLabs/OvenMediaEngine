#define OV_LOG_TAG "RtspServer"

#include "rtsp_library.h"
#include "rtsp_server.h"
#include "rtp/rtp_udp_track.h"
#include "rtp/rtp_h264_track.h"
#include "rtp/rtp_opus_track.h"
#include "rtp/rtp_mpeg4_track.h"
#include "rtp/rtp_aac_track.h"

#include <modules/h264/h264_nal_unit_types.h>

#include <limits>

constexpr char RtspServer::ClassName[];

template<typename T, typename ... Args>
std::unique_ptr<T> CreateTrack(const RtspServer::StreamTrackInfo *stream_track, RtspServer &rtsp_server, Args && ... args)
{
    std::unique_ptr<T> rtp_track;
    const auto media_type = stream_track->media_track_.GetMediaType();
    const auto media_codec = stream_track->media_track_.GetCodecId();
    OV_ASSERT2(stream_track->media_track_.GetTimeBase().GetNum() == 1);
    if (stream_track->media_track_.GetTimeBase().GetNum() != 1)
    {
        return nullptr;
    }
    int32_t clock_frequency = stream_track->media_track_.GetTimeBase().GetDen();
    OV_ASSERT2(clock_frequency > 0);
    if (clock_frequency <= 0)
    {
        return nullptr;
    }
    switch (media_type)
    {
    case cmn::MediaType::Video:
    case cmn::MediaType::Audio:
        switch (media_codec)
        {
        case cmn::MediaCodecId::H264:
            {
                using TrackType = RtpH264Track<T>;
                auto rtp_h264_track = TrackType::template Create<TrackType>(rtsp_server,
                    media_type,
                    media_codec,
                    stream_track->stream_id_,
                    stream_track->media_track_.GetId(),
                    static_cast<uint32_t>(clock_frequency),
                    std::forward<Args>(args) ...);
                if (rtp_h264_track &&
                    stream_track->sdp_format_parameters_ &&
                    stream_track->sdp_format_parameters_->GetType() == SdpFormatParameters::Type::H264)
                {
                    auto const &h264_sdp_format_parameters = *static_cast<H264SdpFormatParameters*>(stream_track->sdp_format_parameters_.get());
                    auto &h264_sps_pps_tracker = rtp_h264_track->GetH264SpsPpsTracker();
#if defined(PARANOID_STREAM_VALIDATION)
                    auto &h264_bitstream_analyzer = rtp_h264_track->GetH264BitstreamAnalyzer();
#endif
                    {
                        const auto &sps = h264_sdp_format_parameters.sps_;
                        if (sps.empty() == false)
                        {
                            h264_sps_pps_tracker.AddSps(sps.data(), sps.size());
#if defined(PARANOID_STREAM_VALIDATION)
                            h264_bitstream_analyzer.ValidateNalUnit(sps.data() + 1, sps.size() - 1, H264NalUnitType::Sps, *sps.data());
#endif
                        }
                    }
                    {
                        const auto &pps = h264_sdp_format_parameters.pps_;
                        if (pps.empty() == false)
                        {
                            h264_sps_pps_tracker.AddPps(pps.data(), pps.size());
#if defined(PARANOID_STREAM_VALIDATION)
                            h264_bitstream_analyzer.ValidateNalUnit(pps.data() + 1, pps.size() - 1, H264NalUnitType::Pps, *pps.data());
#endif
                        }
                    }
                }
                rtp_track.reset(static_cast<T*>(rtp_h264_track.release()));
            }
            break;
        case cmn::MediaCodecId::Opus:
            {
                using TrackType = RtpOpusTrack<T>;
                rtp_track = TrackType::template Create<TrackType>(rtsp_server,
                    media_type,
                    media_codec,
                    stream_track->stream_id_,
                    stream_track->media_track_.GetId(),
                    static_cast<uint32_t>(clock_frequency),
                    std::forward<Args>(args) ...);
            }
            break;
        case cmn::MediaCodecId::Aac:
            {
                using TrackType = RtpAacTrack<T>;
                auto rtp_aac_track = TrackType::template Create<TrackType>(rtsp_server,
                    media_type,
                    media_codec,
                    stream_track->stream_id_,
                    stream_track->media_track_.GetId(),
                    static_cast<uint32_t>(clock_frequency),
                    std::forward<Args>(args) ...);
                if (rtp_aac_track &&
                    stream_track->sdp_format_parameters_ &&
                    stream_track->sdp_format_parameters_->GetType() == SdpFormatParameters::Type::Mpeg4)
                {
                    auto const &aac_sdp_format_parameters = *static_cast<Mpeg4SdpFormatParameters*>(stream_track->sdp_format_parameters_.get());
                    rtp_aac_track->SetSdpFormatParameters(aac_sdp_format_parameters);
                }
                rtp_track.reset(static_cast<T*>(rtp_aac_track.release()));
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    if (rtp_track && rtp_track->Initialize(stream_track->media_track_) == false)
    {
        rtp_track.reset();
    }
    return rtp_track;
}

void RtspServer::OnConnected(const std::shared_ptr<ov::Socket> &remote)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto request_iterator = requests_.find(remote.get());
    if (request_iterator != requests_.end())
    {
        return;
    }
    requests_.emplace(remote.get(), std::make_shared<RtspConnection>(*this, remote));
}

void RtspServer::OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
                                const ov::SocketAddress &address,
                                const std::shared_ptr<const ov::Data> &data)
{
    std::shared_ptr<RtspConnection> rtsp_connection;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto request_iterator = requests_.find(remote.get());
        if (request_iterator != requests_.end())
        {
            rtsp_connection = request_iterator->second;
        }
    }
    rtsp_connection->AppendRequestData(data);
}

void RtspServer::OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
                                PhysicalPortDisconnectReason reason,
                                const std::shared_ptr<const ov::Error> &error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto request_iterator = requests_.find(remote.get());
    if (request_iterator != requests_.end())
    {
        requests_.erase(request_iterator);
    }
}

bool RtspServer::Disconnect(const ov::String &app_name, uint32_t stream_id)
{
    return true;
}

void RtspServer::DeleteStream(const std::lock_guard<std::mutex> &lock, std::unordered_map<std::string, uint32_t>::iterator &stream_id_iterator)

{
    // Not sure if shut down the existing connections and continue or fail, currently erase everything
    for (auto stream_track_iterator = stream_tracks_.begin(); stream_track_iterator != stream_tracks_.end();)
    {
        if (stream_track_iterator->second.stream_id_ == stream_id_iterator->second)
        {
            stream_track_iterator = stream_tracks_.erase(stream_track_iterator);
        }
        else
        {
            ++stream_track_iterator;    
        }
    }
    for (auto session_track_iterator = session_tracks_.begin(); session_track_iterator != session_tracks_.end();)
    {
        if (session_track_iterator->first == stream_id_iterator->second)
        {
            session_track_iterator = session_tracks_.erase(session_track_iterator);
        }
        else
        {
            ++session_track_iterator;
        }
    }
    stream_contexts_.erase(stream_id_iterator->second);
    stream_ids_.erase(stream_id_iterator);
}

bool RtspServer::OnStreamAnnounced(const std::string_view &app_name, 
        const std::string_view &stream_name,
        const RtspMediaInfo &media_info)
{
    // Not sure if announce when the stream arrives or wait till all the tracks are set up, currently announce as soon as it shows up
    const auto stream_path = std::string(app_name.data(), app_name.size()).append("/").append(std::string(stream_name.data(), stream_name.size()));
    
    std::lock_guard<std::mutex> lock(mutex_);
    {
        auto existing_stream_iterator = stream_ids_.find(stream_path);
        if (existing_stream_iterator != stream_ids_.end())
        {
            logtw("Stream(%s) has already been announced by application(%s)", std::string(stream_name.data(), stream_name.size()).c_str(), std::string(app_name.data(), app_name.size()).c_str());
            // Not sure if shut down the existing connections and continue or fail, currently erase everything
            DeleteStream(lock, existing_stream_iterator);
        }
    }
    auto stream_id = stream_ids_.emplace(stream_path, ov::Random::GenerateUInt32()).first->second;
    auto stream_context_iterator = stream_contexts_.emplace(stream_id, RtspStreamContext
    {
        .session_id_ = ov::Random::GenerateUInt32(),
        .track_count_ = media_info.tracks_.size()
    }).first;
    for (const auto &track : media_info.tracks_)
    {
        if (track.second != 0)
        {
            auto payload_iterator = media_info.payloads_.find(track.second);
            if (payload_iterator != media_info.payloads_.end())
            {
                const auto &payload = payload_iterator->second;
                // RTSP tracks can't specify a numerator for the clock]
                OV_ASSERT2(payload.GetTimeBase().GetNum() == 1 && payload.GetTimeBase().GetDen() > 0);
                stream_tracks_.emplace(stream_path + "/" + track.first, StreamTrackInfo
                    {
                        .stream_id_ = stream_id,
                        .media_track_ = payload,
                        .sdp_format_parameters_ = GetFormatParameters(media_info, track.second), 
                    });
            }
            else
            {
                logte("Payload type %u used by track %s has not been specified", track.second, track.first.c_str());
            }
        }
        else
        {
            logte("Track %s has no payload type", track.first.c_str());
        }
        
    }
    uint32_t routed_stream_id;
    info::application_id_t routed_application_id;
    for (const auto &observer : observers_)
    {
        observer->OnStreamAnnounced(ov::String(app_name.data(), app_name.size()), ov::String(stream_name.data(), stream_name.size()), media_info, routed_application_id, routed_stream_id);
        stream_context_iterator->second.routes_.emplace_back(observer, routed_application_id, routed_stream_id);
    }
    return true;
}

bool RtspServer::OnUdpStreamTrackSetup(const std::string_view &rtsp_uri,
        uint16_t rtp_client_port,
        uint16_t rtcp_client_port,
        uint32_t &session_id,
        RtpUdpTrack **track)
{
    const auto track_path = std::string(rtsp_uri.data(), rtsp_uri.size());
    
    std::lock_guard<std::mutex> lock(mutex_);
    const auto *stream_track = FindTrackStream(lock, track_path);
    if (stream_track == nullptr)
    {
        return false;
    }

    auto *session_tracks = FindSessionTracks(lock, stream_track->stream_id_);
    if (session_tracks == nullptr)
    {
        return false;
    }

    auto rtp_udp_track = CreateTrack<RtpUdpTrack>(stream_track, *this, rtp_udp_port_range_);
    if (rtp_udp_track == nullptr)
    {
        return false;
    }
    if (track)
    {
        *track = rtp_udp_track.get();
    }
    session_tracks->emplace_back(std::move(rtp_udp_track));
    return true; 
}

bool RtspServer::OnTcpStreamTrackSetup(const std::string_view &rtsp_uri,
    uint16_t rtp_channel,
    uint16_t rtcp_channel,
    uint32_t &session_id,
    RtpTcpTrack **track)
{
    const auto track_path = std::string(rtsp_uri.data(), rtsp_uri.size());
    
    std::lock_guard<std::mutex> lock(mutex_);
    const auto *stream_track = FindTrackStream(lock, track_path);
    if (stream_track == nullptr)
    {
        return false;
    }

    auto *session_tracks = FindSessionTracks(lock, stream_track->stream_id_);
    if (session_tracks == nullptr)
    {
        return false;
    }

    std::unique_ptr<RtpTcpTrack> rtp_tcp_track = CreateTrack<RtpTcpTrack>(stream_track, *this, rtp_channel, rtcp_channel);
    if (rtp_tcp_track == nullptr)
    {
        return false;
    }
    if (track)
    {
        *track = rtp_tcp_track.get();
    }
    session_tracks->emplace_back(std::move(rtp_tcp_track));
    return true;
}

RtspServer::StreamTrackInfo *RtspServer::FindTrackStream(const std::lock_guard<std::mutex> &lock,
    const std::string &track_path)
{
    auto stream_track_iterator = stream_tracks_.find(track_path);
    if (stream_track_iterator == stream_tracks_.end())
    {
        logte("Cannot setup track %s since it does not belong to an announced stream", track_path.c_str());
        return nullptr;
    }
    return &stream_track_iterator->second;
}

std::vector<std::unique_ptr<RtpTrack>> *RtspServer::FindSessionTracks(const std::lock_guard<std::mutex> &lock,
    uint32_t stream_id)
{
    auto stream_iterator = stream_contexts_.find(stream_id);
    if (stream_iterator == stream_contexts_.end())
    {
        logte("No stream context was found for stream %u", stream_id);
        return nullptr;
    }
    auto session_id = stream_iterator->second.session_id_;
    return &session_tracks_[session_id];
}

bool RtspServer::OnStreamTeardown(const std::string_view &app_name, 
    const std::string_view &stream_name)
{
    const auto stream_path = std::string(app_name.data(), app_name.size()).append("/").append(std::string(stream_name.data(), stream_name.size()));
    std::lock_guard<std::mutex> lock(mutex_);
    auto stream_iterator = stream_ids_.find(stream_path);
    if (stream_iterator != stream_ids_.end())
    {
        auto stream_context_iterator = stream_contexts_.find(stream_iterator->second);
        if (stream_context_iterator == stream_contexts_.end())
        {
            logtw("No context found for stream(%d)(%s) / application(%s)", stream_iterator->second, std::string(stream_name.data(), stream_name.size()).c_str(), std::string(app_name.data(), app_name.size()).c_str());
            return false;
        }
        for (const auto &route : stream_context_iterator->second.routes_)
        {
            std::get<0>(route)->OnStreamTeardown(std::get<1>(route), std::get<2>(route));
        }
        DeleteStream(lock, stream_iterator);
        return true;
    }
    logtw("Teardown requested for unkown stream(%s) / application(%s)", std::string(stream_name.data(), stream_name.size()).c_str(), std::string(app_name.data(), app_name.size()).c_str());
    return false;
}

bool RtspServer::OnVideoData(uint32_t stream_id,
        uint8_t track_id,
        uint32_t timestamp,
        const std::shared_ptr<std::vector<uint8_t>> &data,
        uint8_t flags, 
        std::unique_ptr<FragmentationHeader> fragmentation_header)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto stream_context_iterator = stream_contexts_.find(stream_id);
    if (stream_context_iterator == stream_contexts_.end())
    {
        return false;
    }
    for (const auto &route : stream_context_iterator->second.routes_)
    {
        std::get<0>(route)->OnVideoData(std::get<1>(route), std::get<2>(route), track_id, timestamp, data, flags, std::move(fragmentation_header));
    }
    return true;
}

bool RtspServer::OnAudioData(uint32_t stream_id,
        uint8_t track_id,
        uint32_t timestamp,
        const std::shared_ptr<std::vector<uint8_t>> &data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto stream_context_iterator = stream_contexts_.find(stream_id);
    if (stream_context_iterator == stream_contexts_.end())
    {
        return false;
    }
    for (const auto &route : stream_context_iterator->second.routes_)
    {
        std::get<0>(route)->OnAudioData(std::get<1>(route), std::get<2>(route), track_id, timestamp, data);
    }
    return true;
}

int32_t operator-(const struct timeval &first, const struct timeval &second)
{
    auto seconds = first.tv_sec - second.tv_sec;
    uint32_t first_usec = first.tv_usec;
    if (first_usec < second.tv_usec)
    {
        seconds -= 1;
        first_usec += 1000000;
    }
    return seconds * 1000000 + (first_usec - second.tv_usec);
}

void RtspServer::OnRtcpSenderReport(uint32_t stream_id,
    uint8_t track_id,
    const RtcpSenderReport &rtcp_sender_report)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto stream_context_iterator = stream_contexts_.find(stream_id);
    if (stream_context_iterator == stream_contexts_.end())
    {
        return;
    }
    auto& stream_context = stream_context_iterator->second;
    auto& track_timestamp = stream_context.track_timestamps_[track_id];
    track_timestamp.rtp_timestamp_ = rtcp_sender_report.rtp_timestamp_;
    track_timestamp.ntp_timestamp_ = rtcp_sender_report.ntp_timestamp_;
    if (stream_context.stream_offsets_initialized_ == false &&
        stream_context.track_count_ > 1 &&
        stream_context.track_timestamps_.size() == stream_context.track_count_)
    {
        // We have all the timestamps, with this we should be able to obtain the initial track offsets:
        // 1) sort them by ntp time
        // 2) subtract the ntp times
        // 3) get the offsets by setting the differences
        std::vector<std::reference_wrapper<RtspStreamTimestamp>> initial_timestamps;
        for (auto &track_timestamp : stream_context.track_timestamps_)
        {
            initial_timestamps.emplace_back(track_timestamp.second);
        }
        std::sort(initial_timestamps.begin(), initial_timestamps.end(), [](const auto &first, const auto &second) 
        {
            const auto &first_timeval = first.get().ntp_timestamp_, &second_timeval = second.get().ntp_timestamp_;
            if (first_timeval.tv_sec == second_timeval.tv_sec)
            {
                return first_timeval.tv_usec == second_timeval.tv_usec;
            }
            return false;
        });
        auto session_track_iterator = session_tracks_.find(stream_context.session_id_);
        if (session_track_iterator == session_tracks_.end())
        {
            return;
        }
        // Not calculate the offsets
        const auto& smallest_timestamp = initial_timestamps.front().get();
        auto timestamp_iterator = initial_timestamps.begin();
        ++timestamp_iterator;
        while (timestamp_iterator != initial_timestamps.end())
        {
            auto &timestamp = (*timestamp_iterator).get();
            timestamp.offset_ = timestamp.ntp_timestamp_ - smallest_timestamp.ntp_timestamp_;
            ++timestamp_iterator;
        }
        stream_context.stream_offsets_initialized_ = true;
    }
}