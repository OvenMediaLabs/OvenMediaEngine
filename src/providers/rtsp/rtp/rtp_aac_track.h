#pragma once

#include "rtp_mpeg4_track.h"
    
template<typename T>
class RtpAacTrack : public RtpMpeg4Track<T>
{
    using AccessUnit = typename RtpMpeg4Track<T>::AccessUnit;
    using Base = RtpMpeg4Track<T>;

    // https://wiki.multimedia.cx/index.php/ADTS
#pragma pack(push, 1)
    struct AdtsHeader
    {
        uint8_t sync_word_high = 0xff;
        /* 
            7-4: 0xf part of syncword,
            3: 0 MPEG-4 version
            2-1: 0 layer
            0: 0 no CRC
        */
        uint8_t sync_word_low_version_layer_protection = 0b11110001;
        /*
            7-6: profile
            5-2: MPEG-4 sampling frequency index
            1: private
            0: high bit of MPEG-4 channel configuration
        */
        uint8_t profile_sampling_freqency_private_channel_high_ = 0b01000000;
        /*
            7-6: lower 2 bits of MPEG-4 channel configuration
            5: originality
            4: home
            3: copyrighted id
            2: copyrighted id start
            1-0: 2 highest bits of frame size
        */
        uint8_t channel_low_four_zero_frame_size_high_ = 0;
        uint8_t frame_size_middle_ = 0; // Middle 8 bits of frame size
        /*
            7-5: lower 3 bits of frame size
            4-0: higher 5 bits of buffer fullness
        */
        uint8_t frame_size_low_fullness_high_ = 0;
        /*
            7-2: lower 6 bits of buffer fullness
            1:0: number of AAC frames minus 1
        */
        uint8_t fullness_low_num_frames_ = 0;

        bool SetSampleRate(uint32_t sample_rate)
        {
            uint8_t sampling_frequency_index;
            if (Mpeg4SamplingRateIndex(sample_rate, sampling_frequency_index))
            {
                profile_sampling_freqency_private_channel_high_ |= sampling_frequency_index << 2;
                return true;
            }
            return false;
        }

        void SetChannelConfiguration(uint8_t channel_configuration)
        {
            profile_sampling_freqency_private_channel_high_ |= (channel_configuration & 0b100) >> 2;
            channel_low_four_zero_frame_size_high_ |= (channel_configuration & 0b11) << 6;
        }

        void SetFrameSize(uint16_t frame_size)
        {
            // The size field is only 13 bits wide
            OV_ASSERT2(frame_size <= 0x7ff);
            channel_low_four_zero_frame_size_high_ &= 0b11111100;
            channel_low_four_zero_frame_size_high_ |= (frame_size & 0x1800) >> 11;
            frame_size_middle_ = (frame_size & 0x7f8) >> 3;
            frame_size_low_fullness_high_ &= 0b00011111;
            frame_size_low_fullness_high_ |= (frame_size & 0b111) << 5;
#if defined(DEBUG)
            uint16_t reconstructed_frame_size = ((channel_low_four_zero_frame_size_high_ & 0b11) << 11)
                | (frame_size_middle_ << 3)
                | ((frame_size_low_fullness_high_ & 0b11100000) >> 5);
            OV_ASSERT2(reconstructed_frame_size == frame_size);
#endif
        }
    };
#pragma pack(pop)

    static_assert(sizeof(AdtsHeader) == 7, "AdtsHeader size w/o CRC must be 7 bytes");

public:
    using Base::Base;

    bool AddRtpPayload(const RtpPacketHeader &rtp_packet_header, uint8_t *rtp_payload, size_t rtp_payload_length) override
    {
        // Both AAC modes (lbr https://tools.ietf.org/html/rfc3640#section-3.3.5 and hbr https://tools.ietf.org/html/rfc3640#section-3.3.6), 
        // must have a two bite prefix so we can asume that it is there
        BitReader bit_reader(rtp_payload + 2, rtp_payload_length - 2);
        uint16_t access_unit_header_section_size = ntohs(*reinterpret_cast<uint16_t*>(rtp_payload));
        bool first_access_unit_header = true;
        uint16_t access_unit_size, access_unit_index, access_unit_index_delta;
        std::vector<AccessUnit> access_units;
        size_t total_size = 0;
        while (bit_reader.BitsConsumed() < access_unit_header_section_size)
        {
            if (bit_reader.ReadBits(Base::mpeg4_sdp_format_parameters_.size_length_, access_unit_size) == false)
            {
                return false;
            };
            if (first_access_unit_header)
            {
                if (bit_reader.ReadBits(Base::mpeg4_sdp_format_parameters_.index_length_, access_unit_index) == false)
                {
                    return false;
                }
                first_access_unit_header = false;
            }
            else
            {
                if (bit_reader.ReadBits(Base::mpeg4_sdp_format_parameters_.index_delta_length_, access_unit_index_delta) == false)
                {
                    return false;
                }
                access_unit_index += access_unit_index_delta + 1;
            }
            if (Base::mpeg4_sdp_format_parameters_.cts_delta_length_)
            {
                bool has_cts;
                if (bit_reader.ReadBit(has_cts) == false)
                {
                    return false;
                }
                if (has_cts)
                {
                    uint64_t cts;
                    if (bit_reader.ReadBits(Base::mpeg4_sdp_format_parameters_.cts_delta_length_, cts) == false)
                    {
                        return false;
                    }
                }
            }
            if (Base::mpeg4_sdp_format_parameters_.dts_delta_length_)
            {
                bool has_dts;
                if (bit_reader.ReadBit(has_dts) == false)
                {
                    return false;
                }
                if (has_dts)
                {
                    uint64_t dts;
                    if (bit_reader.ReadBits(Base::mpeg4_sdp_format_parameters_.cts_delta_length_, dts) == false)
                    {
                        return false;
                    }
                }
            }
            total_size += access_unit_size;
            access_units.emplace_back(AccessUnit
            {
                .size_ = static_cast<uint16_t>(access_unit_size),
                .index_ = static_cast<uint16_t>(access_unit_index)
            });
        }
        if (bit_reader.BytesConsumed() + 2 + total_size != rtp_payload_length)
        {
            // The only valid case for this is if we have a fragmented frame, which is supported by AAC-hbr,
            // as per RFC https://tools.ietf.org/html/rfc3640#section-3.2.3.1, only a single fragment can be present
            if (access_units.size() != 1)
            {
                return false;
            }
            if (fragmented_access_unit_ == nullptr)
            {
                // Since we know the size of the access unit we can allocate the buffer with the proper size
                const auto frame_size = access_units[0].size_ + sizeof(adts_header_);
                adts_header_.SetFrameSize(frame_size);
                fragmented_access_unit_ = std::make_shared<std::vector<uint8_t>>(frame_size);
                memcpy(fragmented_access_unit_->data(), &adts_header_, sizeof(adts_header_));
                fragmented_access_unit_timestamp_ = rtp_packet_header.timestamp_;
                fragmented_access_unit_length_ = sizeof(adts_header_);
            }
            // Number of bytes available for this access unit in the current RTP packet
            const size_t access_unit_payload_length = rtp_payload_length - 2 - bit_reader.BytesConsumed();
            if (access_unit_payload_length > fragmented_access_unit_->size() - fragmented_access_unit_length_)
            {
                return false;
            }
            memcpy(fragmented_access_unit_->data() + fragmented_access_unit_length_, rtp_payload + 2 + bit_reader.BytesConsumed(), access_unit_payload_length);
            fragmented_access_unit_length_ += access_unit_payload_length;
            if (fragmented_access_unit_length_ == fragmented_access_unit_->size())
            {
                if (rtp_packet_header.marker_ == 0)
                {
                    // We have assembled the access unit but the marker bit is not present,
                    // something has gone wrong
                    return 0;
                }
                T::rtsp_server_.OnAudioData(T::stream_id_,
                    T::track_id_,
                    fragmented_access_unit_timestamp_ - T::first_timestamp_,
                    fragmented_access_unit_);
                fragmented_access_unit_ = nullptr;
                fragmented_access_unit_length_ = 0;
                fragmented_access_unit_timestamp_ = 0;
            }
            return true;
        }
        size_t access_unit_offset = 2 + bit_reader.BytesConsumed();
        const uint8_t *access_unit_payload = rtp_payload + access_unit_offset;
        const auto base_timestamp = rtp_packet_header.timestamp_ - T::first_timestamp_;
        for (const auto &access_unit : access_units)
        {
            const auto frame_size = access_unit.size_ + sizeof(adts_header_);
            adts_header_.SetFrameSize(frame_size);
            auto media_packet = std::make_shared<std::vector<uint8_t>>(frame_size);
            memcpy(media_packet->data(), &adts_header_, sizeof(adts_header_));
            memcpy(media_packet->data() + sizeof(adts_header_), access_unit_payload, access_unit.size_);
            T::rtsp_server_.OnAudioData(T::stream_id_,
                T::track_id_,
                base_timestamp + access_unit.index_ * constant_duration_,
                media_packet);
            access_unit_payload += access_unit.size_;
        }
        return true;
    }

    bool Initialize(const MediaTrack &media_track) override
    {
        if (adts_header_.SetSampleRate(T::clock_frequency_) == false)
        {
            return false;
        }
        // Currently this should be safe since MediaTrack only supports mono/stereo and those map to 1/2 in MPEG-4
        adts_header_.SetChannelConfiguration(media_track.GetChannel().GetCounts());
        // The below code is disabled since FFmpeg seems to push out invalid config - it sends AudioSpecificConfig instead of StreamMuxConfig
#if 0
        // If the config attribute was specified in SDP parse it, else try to fill the ADTS header with best effor guesses
        if (Base::mpeg4_sdp_format_parameters_.config_.empty() == false)
        {
            BitReader bit_reader(Base::mpeg4_sdp_format_parameters_.config_.data(), Base::mpeg4_sdp_format_parameters_.config_.size());
            uint8_t audio_mux_version;
             if (bit_reader.ReadBit(audio_mux_version) && audio_mux_version == 0)
             {
                 uint8_t same_time_framing, num_sub_frames, num_programs;
                 if (bit_reader.ReadBits(1, same_time_framing)
                    && bit_reader.ReadBits(6, num_sub_frames)
                    && bit_reader.ReadBits(4, num_programs))
                 {
                    num_programs += 1;
                    for (uint8_t program = 0; program < num_programs; ++program)
                    {
                        uint8_t num_layers;
                        if (bit_reader.ReadBits(3, num_layers))
                        {
                            num_layers += 1;
                            for (uint8_t layer = 0; layer < num_layers; ++layer)
                            {
                                if (program == 0 && layer == 0)
                                {
                                    uint8_t audio_object_type, sampling_requency_index;
                                    if (bit_reader.ReadBits(5, audio_object_type) && bit_reader.ReadBits(4, sampling_requency_index))
                                    {
                                        adts_header_.profile_ = audio_object_type - 1;
                                        if (sampling_requency_index != 15)
                                        {
                                            adts_header_.sampling_frequency_index_ = sampling_requency_index;
                                        }
                                        else
                                        {
                                            uint32_t sampling_frequency;
                                            bit_reader.ReadBits(24, sampling_frequency);
                                        }
                                        uint8_t channel_configuration;
                                        if (bit_reader.ReadBits(4, channel_configuration))
                                        {
                                            adts_header_.channel_configuration_ = channel_configuration;
                                        }
                                    }
                                }
                            }
                        }
                    }
                 }
             }
        }
#endif
        return true;
    }

private:
    uint32_t constant_duration_ = 1024;
    uint32_t fragmented_access_unit_timestamp_;
    std::shared_ptr<std::vector<uint8_t>> fragmented_access_unit_;
    size_t fragmented_access_unit_length_;
    AdtsHeader adts_header_;
};
