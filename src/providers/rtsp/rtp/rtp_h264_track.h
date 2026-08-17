#pragma once

#include "../rtsp_library.h"
#include "rtp.h"

#include <modules/h264/h264.h>
#include <modules/h264/h264_sps_pps_tracker.h>
#include <modules/h264/h264_nal_unit_bitstream_parser.h>

#if defined(PARANOID_STREAM_VALIDATION)
#include <modules/h264/h264_bitstream_analyzer.h>
#endif

#include <base/ovlibrary/byte_ordering.h>

/*
    NOTE: The H.264 RTP track emits video with AnnexB start codes so that it is easier to ingest it for
    decoders, and publishers of bypassed streams
*/
template<typename T>
class RtpH264Track : public T
{
    static constexpr uint8_t four_byte_prefix[4] = { 0, 0, 0, 1 };
    static constexpr uint8_t three_byte_prefix[3] = { 0, 0, 1 };

public:
    using T::T;

#if defined(PARANOID_STREAM_VALIDATION)
    H264BitstreamAnalyzer &GetH264BitstreamAnalyzer()
    {
        return h264_bitstream_analyzer_;
    }
#endif

    H264SpsPpsTracker &GetH264SpsPpsTracker()
    {
        return sps_pps_tracker_;
    }

protected:
    void OnNalUnit(uint8_t nal_unit_type, const uint8_t *nal_unit, size_t length)
    {
        switch (nal_unit_type)
        {
        case static_cast<uint8_t>(H264NalUnitType::Sps):
            sps_pps_tracker_.AddSps(nal_unit, length);
            break;
        case static_cast<uint8_t>(H264NalUnitType::Pps):
            sps_pps_tracker_.AddPps(nal_unit, length);
            break;
        }
    }

    void GetSpsPpsForSlice(uint8_t nal_unit_type, const uint8_t *nal_unit_payload, size_t length, std::vector<uint8_t> &media_packet, FragmentationHeader &fragmentation_header)
    {
        if (nal_unit_type == H264NalUnitType::IdrSlice && length)
        {
            H264NalUnitBitstreamParser parser(nal_unit_payload + 1, length - 1);
            uint32_t first_mb_in_slice, slice_type, pps_id = 0;
            if (parser.ReadUEV(first_mb_in_slice) && parser.ReadUEV(slice_type) && parser.ReadUEV(pps_id))
            {
                auto *pps_sps = sps_pps_tracker_.GetPpsSps(pps_id);
                if (pps_sps)
                {
                    // SPS + AnnexB prefix
                    media_packet.insert(media_packet.end(), four_byte_prefix, four_byte_prefix + sizeof(four_byte_prefix));
                    fragmentation_header.fragmentation_offset.emplace_back(media_packet.size());
                    media_packet.insert(media_packet.end(), pps_sps->second.begin(), pps_sps->second.end());
                    fragmentation_header.fragmentation_length.emplace_back(pps_sps->second.size());
                    fragmentation_header.fragmentation_pl_type.emplace_back(static_cast<uint8_t>(H264NalUnitType::Sps));
                    // PPS + AnnexB prefix
                    media_packet.insert(media_packet.end(), four_byte_prefix, four_byte_prefix + sizeof(four_byte_prefix));
                    fragmentation_header.fragmentation_offset.emplace_back(media_packet.size());
                    media_packet.insert(media_packet.end(), pps_sps->first.begin(), pps_sps->first.end());
                    fragmentation_header.fragmentation_length.emplace_back(pps_sps->first.size());
                    fragmentation_header.fragmentation_pl_type.emplace_back(static_cast<uint8_t>(H264NalUnitType::Pps));
                }
            }
            else
            {
                logte("Failed to find SPS/PPS pair for slice with type %u referencing PPS %u", nal_unit_type, pps_id);
            }
            
        }
    }

    bool AddRtpPayload(const RtpPacketHeader &rtp_packet_header, uint8_t *rtp_payload, size_t rtp_payload_length) override
    {
        bool is_key_frame = false;
        size_t payload_offset = 0;
        const uint8_t type_byte = rtp_payload[0];
        const uint8_t packet_type = type_byte & kH264NalUnitTypeMask;
        // Media packet and fragmentation header will be allocated on need
        std::unique_ptr<FragmentationHeader> fragmentation_header;
        std::shared_ptr<std::vector<uint8_t>> media_packet;
        if (1 <= packet_type && packet_type <= 23 )
        {
            // Singe NAL unit
            const auto nal_unit_type = packet_type;
            OV_ASSERT2(IsValidH264NalUnitType(nal_unit_type));
            OnNalUnit(nal_unit_type, rtp_payload, rtp_payload_length);
#if defined(PARANOID_STREAM_VALIDATION)
            h264_bitstream_analyzer_.ValidateNalUnit(rtp_payload + 1, rtp_payload_length - 1, type_byte);
#endif
            if (nal_unit_type == H264NalUnitType::IdrSlice || nal_unit_type == H264NalUnitType::NonIdrSlice)
            {
                is_key_frame = nal_unit_type == H264NalUnitType::IdrSlice;
                fragmentation_header = std::make_unique<decltype(fragmentation_header)::element_type>();
                media_packet = std::make_shared<decltype(media_packet)::element_type>();
                if (nal_unit_type == H264NalUnitType::IdrSlice)
                {
                    GetSpsPpsForSlice(nal_unit_type, rtp_payload, rtp_payload_length, *media_packet, *fragmentation_header);
                }
                media_packet->insert(media_packet->end(), three_byte_prefix, three_byte_prefix + sizeof(three_byte_prefix));
                fragmentation_header->fragmentation_offset.emplace_back(media_packet->size());
                fragmentation_header->fragmentation_length.emplace_back(rtp_payload_length);
                fragmentation_header->fragmentation_pl_type.emplace_back(nal_unit_type);
                fragmentation_header->last_fragment_complete = true; 
                media_packet->insert(media_packet->end(), rtp_payload, rtp_payload + rtp_payload_length);
            }
            else
            {
                return true;
            }
        }
        else if (packet_type == 24)
        {
            // STAP-A
            // Skip STAP-A indicator
            payload_offset += 1;
            const uint8_t *nal_unit_bytes = rtp_payload + 1;
            size_t nal_unit_bytes_length = rtp_payload_length - payload_offset, nal_unit_bytes_remaining = nal_unit_bytes_length;
            while (nal_unit_bytes_remaining)
            {
                if (nal_unit_bytes_remaining < 2)
                {
                    logte("Failed to parse incoming STAP-A packet");
                    return false;
                }
                uint16_t nal_unit_size = ntohs(*reinterpret_cast<const uint16_t*>(nal_unit_bytes));
                nal_unit_bytes_remaining -= 2;
                nal_unit_bytes += 2;
                if (nal_unit_size > nal_unit_bytes_remaining)
                {
                    logte("Failed to parse incoming STAP-A packet");
                    return false;
                }
                const auto nal_unit_type = *nal_unit_bytes & kH264NalUnitTypeMask;
                OnNalUnit(nal_unit_type, nal_unit_bytes, nal_unit_size);
#if defined(PARANOID_STREAM_VALIDATION)
                h264_bitstream_analyzer_.ValidateNalUnit(nal_unit_bytes + 1, nal_unit_size - 1, *nal_unit_bytes);
#endif
                if (nal_unit_type == H264NalUnitType::IdrSlice || nal_unit_type == H264NalUnitType::NonIdrSlice)
                {
                    if (media_packet != nullptr)
                    {
                        /*
                            In some scenarios a STAP-A packet can contain multiple slices, if so, push them up
                            as separate media packets
                        */
                       T::rtsp_server_.OnVideoData(T::stream_id_,
                            T::track_id_,
                            rtp_packet_header.timestamp_ - T::first_timestamp_,
                            media_packet,
                            is_key_frame ? static_cast<uint8_t>(RtpVideoFlags::Keyframe) : 0,
                            std::move(fragmentation_header));
                        is_key_frame = false;

                    }
                    if (is_key_frame == false)
                    {
                        is_key_frame = nal_unit_type == H264NalUnitType::IdrSlice;
                    }
                    fragmentation_header = std::make_unique<decltype(fragmentation_header)::element_type>();
                    media_packet = std::make_shared<decltype(media_packet)::element_type>();
                    if (nal_unit_type == H264NalUnitType::IdrSlice)
                    {
                        GetSpsPpsForSlice(nal_unit_type, nal_unit_bytes, nal_unit_size, *media_packet, *fragmentation_header);
                    }
                    media_packet->insert(media_packet->end(), three_byte_prefix, three_byte_prefix + sizeof(three_byte_prefix));
                    fragmentation_header->fragmentation_offset.emplace_back(media_packet->size());
                    fragmentation_header->fragmentation_length.emplace_back(nal_unit_size);
                    fragmentation_header->fragmentation_pl_type.emplace_back(nal_unit_type);
                    fragmentation_header->last_fragment_complete = true;
                    media_packet->insert(media_packet->end(), nal_unit_bytes, nal_unit_bytes + nal_unit_size);
                }
                nal_unit_bytes += nal_unit_size;
                nal_unit_bytes_remaining -= nal_unit_size;
            }
        }
        else if (packet_type == 28)
        {
            // FU-A
            // 
            // 1) Obtain the end marker from the FU-A header
            // 1) Reconstruct the FU-A header byte to be a proper NAL start byte by transferring the 3 upper bits from the FU-A indicator byte if this is the first FU-A packet
            // 2) Make fragmentation header point to the first byte, set completion state depending on the value obtained in 1)
            // 3) If we are assembling FU-A chunks, add the data to internal buffer and continue, otherwise signal the packet
            auto &fu_header = rtp_payload[1];
            const auto nal_unit_type = fu_header & kH264NalUnitTypeMask;
            const bool is_last_fu_a_segment = fu_header & 0b1000000, is_first_fu_a_segment = fu_header & 0b10000000;
            // Skip the FU indicator
            payload_offset += 1;
            if (is_first_fu_a_segment)
            {
                fu_header = nal_unit_type | (type_byte & 0b11100000);
                OV_ASSERT2(IsValidH264NalUnitType(nal_unit_type));
#if defined(PARANOID_STREAM_VALIDATION)
                h264_bitstream_analyzer_.ValidateNalUnit(rtp_payload + payload_offset + 1, rtp_payload_length - payload_offset - 1, fu_header);
#endif
                is_key_frame = nal_unit_type == H264NalUnitType::IdrSlice;
                if (assemble_fu_a_packets_)
                {
                    fu_a_packet_ = std::make_shared<typename decltype(fu_a_packet_)::element_type>();
                    fu_a_fragmentation_header_ = std::make_unique<typename decltype(fu_a_fragmentation_header_)::element_type>();
                    fu_a_packet_is_key_frame = is_key_frame;
                    if (nal_unit_type == H264NalUnitType::IdrSlice)
                    {
                        GetSpsPpsForSlice(nal_unit_type, rtp_payload + payload_offset, rtp_payload_length - payload_offset, *fu_a_packet_, *fu_a_fragmentation_header_);
                    }
                    fu_a_packet_->insert(fu_a_packet_->end(), three_byte_prefix, three_byte_prefix + sizeof(three_byte_prefix));
                    fu_a_fragmentation_header_->fragmentation_offset.emplace_back(fu_a_packet_->size());
                    fu_a_fragmentation_header_->fragmentation_length.emplace_back(0);
                    fu_a_fragmentation_header_->fragmentation_pl_type.emplace_back(nal_unit_type);
                }
                else
                {
                    fragmentation_header = std::make_unique<decltype(fragmentation_header)::element_type>();
                    media_packet = std::make_shared<decltype(media_packet)::element_type>();
                    if (nal_unit_type == H264NalUnitType::IdrSlice)
                    {
                        GetSpsPpsForSlice(nal_unit_type, rtp_payload + payload_offset, rtp_payload_length - payload_offset, *media_packet, *fragmentation_header);
                    }
                    media_packet->insert(media_packet->end(), three_byte_prefix, three_byte_prefix + sizeof(three_byte_prefix));
                }
            }
            else
            {
                // Skip the FU header
                payload_offset += 1;
            }
            if (assemble_fu_a_packets_)
            {
                if (fu_a_packet_ == nullptr)
                {
                    logte("Received FU-A payload without a starting FU-A packet");
                    return false;
                }
                fu_a_packet_->insert(fu_a_packet_->end(), rtp_payload + payload_offset, rtp_payload + rtp_payload_length);
                fu_a_fragmentation_header_->fragmentation_length.back() += rtp_payload_length - payload_offset;
                if (is_last_fu_a_segment == false)
                {
                    return true;
                }
                is_key_frame = fu_a_packet_is_key_frame;
                fu_a_fragmentation_header_->last_fragment_complete = true;
                media_packet = std::move(fu_a_packet_);
                fragmentation_header = std::move(fu_a_fragmentation_header_);
            }
            else
            {
                fragmentation_header->fragmentation_offset.emplace_back(media_packet->size());
                fragmentation_header->fragmentation_length.emplace_back(rtp_payload_length);
                fragmentation_header->fragmentation_pl_type.emplace_back(nal_unit_type);
                fragmentation_header->last_fragment_complete = is_last_fu_a_segment; 
            }
        }
        else
        {
            logte("Unknown RTP H264 packet type %u", packet_type);
            return false;
        }
        if (media_packet && media_packet->empty() == false)
        {
            T::rtsp_server_.OnVideoData(T::stream_id_,
                T::track_id_,
                rtp_packet_header.timestamp_ - T::first_timestamp_,
                media_packet,
                is_key_frame ? static_cast<uint8_t>(RtpVideoFlags::Keyframe) : 0,
                std::move(fragmentation_header));
        }
        return true;
    }

private:
    // TODO(rubu): until the H264 packetizer in the WebRTC end is reworked we must assemble FU-A packets, since the packetizer in the WebRTC
    // end is recreated each time and cannot handle separate FU-A segments in multiple packets
    bool assemble_fu_a_packets_ = true;
    std::shared_ptr<std::vector<uint8_t>> fu_a_packet_;
    std::unique_ptr<FragmentationHeader> fu_a_fragmentation_header_;
    bool fu_a_packet_is_key_frame = false;
    H264SpsPpsTracker sps_pps_tracker_;
#if defined(PARANOID_STREAM_VALIDATION)
    H264BitstreamAnalyzer h264_bitstream_analyzer_;
#endif
};