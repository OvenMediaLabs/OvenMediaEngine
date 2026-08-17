#include "sdp.h"

#include <modules/bitstream/h264/h264_parser.h>
#include <base/ovlibrary/stl.h>
#include <base/ovcrypto/base_64.h>

enum class RtpCodec
{
    Unknown,
    H264,
    H265,
    Opus,
    Mpeg4GenericAudio,
    Mpeg4GenericVideo,
    Mpeg4VideoElementaryStream,
};

bool ParseSdp(const std::vector<uint8_t> &sdp, RtspMediaInfo &rtsp_media_info)
{
    std::vector<std::string_view> lines = Split(sdp, {static_cast<uint8_t>('\r'), static_cast<uint8_t>('\n')});
    int16_t last_payload_type = -1;
    std::unordered_map<uint8_t, RtpCodec> payload_codecs;
    for (const auto &line : lines)
    {
        // https://tools.ietf.org/html/rfc4566 RFC-4566 states what whitespace MUST not be used on any side of = so just access the delimiter directly by index
        if (line.size() >= 2 && line[1] == '=')
        {
            switch (line[0])
            {
            case 'a':
                {
                    constexpr char rtmpmap[] = { 'r', 't', 'p', 'm', 'a', 'p', ':' };
                    constexpr char control[] = { 'c', 'o', 'n', 't', 'r', 'o', 'l', ':' };
                    constexpr char fmtp[] = {'f', 'm', 't', 'p', ':' };
                    if (HasSubstring(line, 2, control))
                    {
                        const auto &control_uri = line.substr(2 + sizeof(control));
                        rtsp_media_info.tracks_.emplace(std::string(control_uri.data(), control_uri.size()), last_payload_type);
                    }
                    else if (HasSubstring(line, 2, rtmpmap))
                    {
                        auto codec_parameters = Split(line.substr(2 + sizeof(rtmpmap)), ' ');
                        // https://tools.ietf.org/html/rfc4566#section-6
                        // a=rtpmap:<payload type> <encoding name>/<clock rate> [/<encoding parameters>]
                        if (codec_parameters.size() >= 2)
                        {
                            uint8_t payload_type = 0;
                            if (Stoi(std::string(codec_parameters[0].data(), codec_parameters[0].size()), payload_type) == false)
                            {
                                continue;
                            }
                            auto codec = Split(codec_parameters[1], '/');
                            if (codec.empty() == false)
                            {
                                auto &payload = rtsp_media_info.payloads_[payload_type];
                                if (codec[0] == "H264")
                                {
                                    payload_codecs[payload_type] = RtpCodec::H264;
                                    payload.SetCodecId(cmn::MediaCodecId::H264);
                                }
                                else if (codec[0] == "H265")
                                {
                                    payload_codecs[payload_type] = RtpCodec::H265;
                                    payload.SetCodecId(cmn::MediaCodecId::H265);
                                }
                                else if (codec[0] == "opus")
                                {
                                    payload_codecs[payload_type] = RtpCodec::Opus;
                                    payload.SetCodecId(cmn::MediaCodecId::Opus);
                                    // For Opus stereo should be detected from the sprop-stereo field
                                    auto &audio_channel = payload.GetChannel();
                                    audio_channel.SetLayout(cmn::AudioChannel::Layout::LayoutMono);
                                }
                                else if (CaseInsensitiveEqual(codec[0],"MPEG4-GENERIC"_str_v)) // The 
                                {
                                    auto payload_iterator = rtsp_media_info.payloads_.find(payload_type);
                                    if (payload_iterator != rtsp_media_info.payloads_.end())
                                    {
                                        if (payload_iterator->second.GetMediaType() == cmn::MediaType::Video)
                                        {
                                            payload_codecs[payload_type] = RtpCodec::Mpeg4GenericVideo;
                                        }
                                        else if (payload_iterator->second.GetMediaType() == cmn::MediaType::Audio)
                                        {
                                            payload_codecs[payload_type] = RtpCodec::Mpeg4GenericAudio;
                                        }
                                    }
                                }
                                else if (CaseInsensitiveEqual(codec[0], "MP4V-ES"))
                                {
                                    payload_codecs[payload_type] = RtpCodec::Mpeg4VideoElementaryStream;
                                }
                                if (codec.size() > 1)
                                {
                                    {
                                        int32_t clock_rate = 0;
                                        if (Stoi(std::string(codec[1].data(), codec[1].size()), clock_rate))
                                        {
                                            payload.SetTimeBase(1, clock_rate);
                                            if (payload.GetMediaType() == cmn::MediaType::Audio)
                                            {
                                                payload.SetSampleRate(clock_rate);
                                            }
                                        }
                                    }
                                    if (codec.size() > 2 && payload.GetMediaType() == cmn::MediaType::Audio)
                                    {
                                        uint8_t channel_count;
                                        if (Stoi(std::string(codec[2].data(), codec[2].size()), channel_count))
                                        {
                                            auto &audio_channel = payload.GetChannel();
                                            switch (channel_count)
                                            {
                                            case 1:
                                                audio_channel.SetLayout(cmn::AudioChannel::Layout::LayoutMono);
                                                break;
                                            case 2:
                                                audio_channel.SetLayout(cmn::AudioChannel::Layout::LayoutStereo);
                                                break;
                                            default:
                                                // Currently nothing else except mono and stereo is supported
                                                return false;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else if (HasSubstring(line, 2, fmtp))
                    {
                        // a=fmtp:<format> <format specific parameters>
                        auto format_components = Split(line.substr(2 + sizeof(fmtp)), ' ');
                        if (format_components.size() > 1)
                        {
                            uint8_t payload_type = 0;
                            if (Stoi(std::string(format_components[0].data(), format_components[0].size()), payload_type) == false)
                            {
                                continue;
                            }
                            RtpCodec rtp_codec = RtpCodec::Unknown;
                            {
                                const auto payload_codec_iterator = payload_codecs.find(payload_type);
                                if (payload_codec_iterator != payload_codecs.end())
                                {
                                    rtp_codec = payload_codec_iterator->second;
                                }
                            }
                            // Depending on the client, maybe everything now has been separated, maybe not, so split everything by ; when going over
                            for (size_t component_index = 1; component_index < format_components.size(); ++component_index)
                            {
                                for (const auto  &format_component : Split(format_components[component_index], ';'))
                                {
                                    auto format_component_part_delimiter = format_component.find('=');
                                    if (format_component_part_delimiter != std::string_view::npos)
                                    {
                                        auto format_component_name = std::string_view(format_component.data(), format_component_part_delimiter);
                                        auto format_component_value = std::string_view(format_component.data() + format_component_part_delimiter + 1, format_component.size() - format_component_part_delimiter - 1);
                                        if (rtp_codec == RtpCodec::H264 && format_component_name == "sprop-parameter-sets")
                                        {
                                            auto parameter_sets = Split(format_component_value, ',');
                                            if (parameter_sets.size() == 2)
                                            {
                                                auto sps_bytes = ov::Base64::Decode(ov::String(parameter_sets[0].data(), parameter_sets[0].size()));
                                                auto pps_bytes = ov::Base64::Decode(ov::String(parameter_sets[1].data(), parameter_sets[1].size()));
                                                auto &payload = rtsp_media_info.payloads_[payload_type];
                                                {
                                                    // Put the SPS/PPS into both - codec extradata (so that it is passed throughout the stack) and in the SDP format parameters for the RTSP server
                                                    const auto sps = std::vector<uint8_t>(sps_bytes->GetDataAs<uint8_t>(), sps_bytes->GetDataAs<uint8_t>() +sps_bytes->GetLength());
                                                    const auto pps = std::vector<uint8_t>(pps_bytes->GetDataAs<uint8_t>(), pps_bytes->GetDataAs<uint8_t>() + pps_bytes->GetLength());
                                                    auto h264_sdp_format_parameters = GetFormatParameters<H264SdpFormatParameters>(rtsp_media_info, payload_type);
                                                    if (h264_sdp_format_parameters)
                                                    {
                                                        h264_sdp_format_parameters->pps_ = pps;
                                                        h264_sdp_format_parameters->sps_ = sps;
                                                    }
                                                    else
                                                    {
                                                        return false;
                                                    }
                                                    H264Extradata h264_extradata;
                                                    h264_extradata.AddSps(sps);
                                                    h264_extradata.AddPps(pps);
                                                    payload.SetCodecComponentData(h264_extradata.Serialize());
                                                }
                                                // Attempt to parse SPS to properly set width/height/frame rate
                                                H264SPS sps;
                                                if (H264Parser::ParseSPS(sps_bytes->GetDataAs<uint8_t>(), sps_bytes->GetLength(), sps))
                                                {
                                                    payload.SetWidth(sps.GetWidth());
                                                    payload.SetHeight(sps.GetHeight());
                                                    payload.SetFrameRate(sps.GetFps());
                                                }
                                                else
                                                {
                                                    // At least clear the variables
                                                    payload.SetWidth(0);
                                                    payload.SetHeight(0);
                                                    payload.SetFrameRate(0);
                                                }
                                            }
                                        }
                                        else if (rtp_codec == RtpCodec::Opus && format_component_name == "sprop-stereo")
                                        {
                                            if (format_component_value.size() && *format_component_value.data() == '1')
                                            {
                                                auto &audio_channel = rtsp_media_info.payloads_[payload_type].GetChannel();
                                                audio_channel.SetLayout(cmn::AudioChannel::Layout::LayoutStereo);
                                            }
                                        }
                                        else if (rtp_codec == RtpCodec::Mpeg4GenericAudio)
                                        {
                                            // RFC https://tools.ietf.org/html/rfc3640 mentions that sizeLength / indexLength / indexDeltaLength and others are camel case,
                                            // but for example FFmpeg sends them as lowercase so use case insensitive comparison here
                                            if (format_component_name == "mode")
                                            {
                                                if (CaseInsensitiveEqual(format_component_value, "AAC-hbr"_str_v) || CaseInsensitiveEqual(format_component_value, "AAC-lbr"_str_v))
                                                {
                                                    auto &payload = rtsp_media_info.payloads_[payload_type];
                                                    payload.SetCodecId(cmn::MediaCodecId::Aac);
                                                }
                                            }
                                            else
                                            {
                                                auto mpeg4_sdp_format_parameters = GetFormatParameters<Mpeg4SdpFormatParameters>(rtsp_media_info, payload_type);
                                                if (mpeg4_sdp_format_parameters)
                                                {
                                                    if (CaseInsensitiveEqual(format_component_name, "sizeLength"_str_v))
                                                    {
                                                        Stoi(std::string(format_component_value.data(), format_component_value.size()), mpeg4_sdp_format_parameters->size_length_);
                                                    }
                                                    else if (CaseInsensitiveEqual(format_component_name, "indexLength"_str_v))
                                                    {
                                                        Stoi(std::string(format_component_value.data(), format_component_value.size()), mpeg4_sdp_format_parameters->index_length_);
                                                    }
                                                    else if (CaseInsensitiveEqual(format_component_name, "indexDeltaLength"_str_v))
                                                    {
                                                        Stoi(std::string(format_component_value.data(), format_component_value.size()), mpeg4_sdp_format_parameters->index_delta_length_);
                                                    }
                                                    else if (CaseInsensitiveEqual(format_component_name, "CTSDeltaLength"_str_v))
                                                    {
                                                        Stoi(std::string(format_component_value.data(), format_component_value.size()), mpeg4_sdp_format_parameters->cts_delta_length_);
                                                    }
                                                    else if (CaseInsensitiveEqual(format_component_name, "DTSDeltaLength"_str_v))
                                                    {
                                                        Stoi(std::string(format_component_value.data(), format_component_value.size()), mpeg4_sdp_format_parameters->dts_delta_length_);
                                                    }
                                                    else if (CaseInsensitiveEqual(format_component_name, "constantSize"_str_v))
                                                    {
                                                        Stoi(std::string(format_component_value.data(), format_component_value.size()), mpeg4_sdp_format_parameters->constant_size_);
                                                    }
                                                    else if (CaseInsensitiveEqual(format_component_name, "config"_str_v))
                                                    {
                                                        mpeg4_sdp_format_parameters->config_.resize((format_component_value.size() + 1) / 2);
                                                        auto hex_config_iterator = format_component_value.begin();
                                                        for (auto config_iterator = mpeg4_sdp_format_parameters->config_.begin(); config_iterator != mpeg4_sdp_format_parameters->config_.end(); ++config_iterator)
                                                        {
                                                            if (hex_config_iterator == format_component_value.end())
                                                            {
                                                                break;
                                                            }
                                                            uint8_t byte = (*hex_config_iterator - '0') << 4;
                                                            ++hex_config_iterator;
                                                            if (hex_config_iterator == format_component_value.end())
                                                            {
                                                                break;
                                                            }
                                                            byte |= (*hex_config_iterator - '0');
                                                            ++hex_config_iterator;
                                                            *config_iterator = byte;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                break;
            case 'c':
                break;
            case 'm':
                {
                    auto components = Split(line.substr(2), static_cast<uint8_t>(' '));
                    if (components.size() == 4)
                    {
                        uint8_t payload_type = 0;
                        if (Stoi(std::string(components[3].data(), components[3].size()), payload_type) == false)
                        {
                            continue;
                        }
                        last_payload_type = payload_type;
                        rtsp_media_info.payloads_[payload_type].SetId(payload_type);
                        if (components[0] == "video")
                        {
                            rtsp_media_info.payloads_[payload_type].SetMediaType(cmn::MediaType::Video);
                        }
                        else if (components[0] == "audio")
                        {
                            rtsp_media_info.payloads_[payload_type].SetMediaType(cmn::MediaType::Audio);
                        }
                    }
                }
                break;
            case 's':
                break;
            }
        }
    }
    return true;
}