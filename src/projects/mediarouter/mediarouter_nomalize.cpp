//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mediarouter_nomalize.h"

#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/aac/aac_adts.h>
#include <modules/bitstream/aac/aac_converter.h>
#include <modules/bitstream/aac/audio_specific_config.h>
#include <modules/bitstream/h264/h264_common.h>
#include <modules/bitstream/h264/h264_decoder_configuration_record.h>
#include <modules/bitstream/h264/h264_parser.h>
#include <modules/bitstream/h265/h265_decoder_configuration_record.h>
#include <modules/bitstream/h265/h265_parser.h>
#include <modules/bitstream/mp3/mp3_parser.h>
#include <modules/bitstream/nalu/nal_stream_converter.h>
#include <modules/bitstream/nalu/nal_unit_fragment_header.h>
#include <modules/bitstream/opus/opus.h>
#include <modules/bitstream/vp8/vp8.h>

#include "mediarouter_private.h"

using namespace cmn;

size_t GetStartCodeSize(const uint8_t *data, size_t length)
{
	if (length < 3)
	{
		return 0;
	}

	if ((data[0] == 0x00) && (data[1] == 0x00))
	{
		if (data[2] == 0x01)
		{
			return 3;
		}
		else if ((length > 3) && (data[2] == 0x00) && (data[3] == 0x01))
		{
			return 4;
		}
	}

	return 0;
}

// H264 : AVCC -> AnnexB, Add SPS/PPS in front of IDR frame
// H265 :
// AAC : Raw -> ADTS
bool MediaRouterNormalize::NormalizeMediaPacket(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	bool result = false;

	if (media_track->GetMediaType() == cmn::MediaType::Data)
	{
		return true;
	}

	switch (media_packet->GetBitstreamFormat())
	{
		case cmn::BitstreamFormat::H264_ANNEXB:
			result = media_packet->GetData() != nullptr && ProcessH264AnnexBStream(stream_info, media_track, media_packet);
			break;
		case cmn::BitstreamFormat::H264_AVCC:
			result = media_packet->GetData() != nullptr && ProcessH264AVCCStream(stream_info, media_track, media_packet);
			break;
		case cmn::BitstreamFormat::H265_ANNEXB:
			result = media_packet->GetData() != nullptr && ProcessH265AnnexBStream(stream_info, media_track, media_packet);
			break;
		case cmn::BitstreamFormat::HVCC:
			result = media_packet->GetData() != nullptr && ProcessH265HVCCStream(stream_info, media_track, media_packet);
			break;
		case cmn::BitstreamFormat::VP8:
			result = media_packet->GetData() != nullptr && ProcessVP8Stream(stream_info, media_track, media_packet);
			break;
		case cmn::BitstreamFormat::AAC_RAW:
			result = media_packet->GetData() != nullptr && ProcessAACRawStream(stream_info, media_track, media_packet);
			break;
		case cmn::BitstreamFormat::AAC_ADTS:
			result = media_packet->GetData() != nullptr && ProcessAACAdtsStream(stream_info, media_track, media_packet);
			break;
		case cmn::BitstreamFormat::OPUS:
			result = media_packet->GetData() != nullptr && ProcessOPUSStream(stream_info, media_track, media_packet);
			break;
		case cmn::BitstreamFormat::MP3:
			result = media_packet->GetData() != nullptr && ProcessMP3Stream(stream_info, media_track, media_packet);
			break;
		// Data Format
		case cmn::BitstreamFormat::ID3v2:
		case cmn::BitstreamFormat::OVEN_EVENT:
		case cmn::BitstreamFormat::CUE:
		case cmn::BitstreamFormat::SCTE35:
		case cmn::BitstreamFormat::AMF:
		case cmn::BitstreamFormat::WebVTT:
			result = true;
			break;
		case cmn::BitstreamFormat::JPEG:
		case cmn::BitstreamFormat::PNG:
		case cmn::BitstreamFormat::WEBP:
			result = true;
			break;
		case cmn::BitstreamFormat::AAC_LATM:
		case cmn::BitstreamFormat::Unknown:
		default:
			logte("Bitstream not supported by inbound");
			break;
	}

	return result;
}

#include <base/ovcrypto/base_64.h>
bool MediaRouterNormalize::ProcessH264AVCCStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	// Everytime : Convert to AnnexB, Make fragment header, Set keyframe flag, Append SPS/PPS nal units in front of IDR frame
	// one time : Parse track info from sps/pps and generate codec_extra_data

	if (media_packet->GetBitstreamFormat() != cmn::BitstreamFormat::H264_AVCC)
	{
		return false;
	}

	if (media_packet->GetPacketType() == cmn::PacketType::SEQUENCE_HEADER)
	{
		// Validation
		auto avc_config = std::make_shared<AVCDecoderConfigurationRecord>();

		if (avc_config->Parse(media_packet->GetData()) == false)
		{
			logte("Could not parse sequence header");
			return false;
		}

		media_track->SetWidth(avc_config->GetWidth());
		media_track->SetHeight(avc_config->GetHeight());

		media_track->SetDecoderConfigurationRecord(avc_config);

		return false;
	}

	// Convert to AnnexB and Insert SPS/PPS if there are no SPS/PPS nal units.
	else if (media_packet->GetPacketType() == cmn::PacketType::NALU)
	{
		auto converted_data = std::make_shared<ov::Data>(media_packet->GetDataLength() + (media_packet->GetDataLength() / 2));
		FragmentationHeader fragment_header;
		size_t nalu_offset = 0;

		const uint8_t START_CODE[4] = {0x00, 0x00, 0x00, 0x01};
		const size_t START_CODE_LEN = sizeof(START_CODE);

		std::shared_ptr<ov::Data> sps_nalu = nullptr, pps_nalu = nullptr;
		bool has_idr = false;
		bool has_sps = false;
		bool has_pps = false;
		bool has_aud = false;

		ov::ByteStream read_stream(media_packet->GetData());
		while (read_stream.Remained() > 0)
		{
			if (read_stream.IsRemained(4) == false)
			{
				logte("Not enough data to parse NAL");
				return false;
			}

			size_t nal_length = read_stream.ReadBE32();
			if (read_stream.IsRemained(nal_length) == false)
			{
				logte("NAL length (%d) is greater than buffer length (%d)", nal_length, read_stream.Remained());
				return false;
			}

			// Convert to AnnexB
			auto nalu = read_stream.GetRemainData(nal_length);

			// Exception handling for encoder that transmits AVCC in non-standard format ([Size][Start Code][NalU])
			if ((nalu->GetLength() > 3 && nalu->GetDataAs<uint8_t>()[0] == 0x00 && nalu->GetDataAs<uint8_t>()[1] == 0x00 && nalu->GetDataAs<uint8_t>()[2] == 0x01) ||
				(nalu->GetLength() > 4 && nalu->GetDataAs<uint8_t>()[0] == 0x00 && nalu->GetDataAs<uint8_t>()[1] == 0x00 && nalu->GetDataAs<uint8_t>()[2] == 0x00 && nalu->GetDataAs<uint8_t>()[3] == 0x01))
			{
				size_t start_code_size = (nalu->GetDataAs<uint8_t>()[2] == 0x01) ? 3 : 4;

				read_stream.Skip(start_code_size);
				nal_length -= start_code_size;
				nalu = read_stream.GetRemainData(nal_length);
			}

			[[maybe_unused]] auto skipped = read_stream.Skip(nal_length);
			OV_ASSERT2(skipped == nal_length);

			H264NalUnitHeader nal_header;
			if (H264Parser::ParseNalUnitHeader(nalu->GetDataAs<uint8_t>(), H264_NAL_UNIT_HEADER_SIZE, nal_header) == true)
			{
				if (nal_header.GetNalUnitType() == H264NalUnitType::IdrSlice)
				{
					media_packet->SetFlag(MediaPacketFlag::Key);
					has_idr = true;
				}
				else if (nal_header.GetNalUnitType() == H264NalUnitType::Sps)
				{
					// logtt("[SPS] %s ", ov::Base64::Encode(nalu).CStr());
					has_sps = true;
					if (sps_nalu == nullptr)
					{
						sps_nalu = std::make_shared<ov::Data>(nalu->GetDataAs<uint8_t>(), nalu->GetLength());
					}
				}
				else if (nal_header.GetNalUnitType() == H264NalUnitType::Pps)
				{
					// logtt("[PPS] %s ", ov::Base64::Encode(nalu).CStr());
					has_pps = true;
					if (pps_nalu == nullptr)
					{
						pps_nalu = std::make_shared<ov::Data>(nalu->GetDataAs<uint8_t>(), nalu->GetLength());
					}
				}
				else if (nal_header.GetNalUnitType() == H264NalUnitType::Aud)
				{
					has_aud = true;
				}
				else if (nal_header.GetNalUnitType() == H264NalUnitType::FillerData)
				{
					// no need to maintain filler data
					continue;
				}
			}
			else
			{
				logte("Could not parse H264 Nal unit header");
				return false;
			}

			converted_data->Append(START_CODE, sizeof(START_CODE));
			nalu_offset += START_CODE_LEN;

			fragment_header.AddFragment(nalu_offset, nalu->GetLength());

			converted_data->Append(nalu);
			nalu_offset += nalu->GetLength();
		}

		media_packet->SetFragHeader(&fragment_header);
		media_packet->SetData(converted_data);
		media_packet->SetBitstreamFormat(cmn::BitstreamFormat::H264_ANNEXB);
		media_packet->SetPacketType(cmn::PacketType::NALU);

		// Update DecoderConfigurationRecord
		if (sps_nalu != nullptr && pps_nalu != nullptr)
		{
			auto new_avc_config = std::make_shared<AVCDecoderConfigurationRecord>();

			new_avc_config->AddSPS(sps_nalu);
			new_avc_config->AddPPS(pps_nalu);

			if (new_avc_config->IsValid())
			{
				auto old_avc_config = std::static_pointer_cast<AVCDecoderConfigurationRecord>(media_track->GetDecoderConfigurationRecord());

				if (media_track->IsValid() == false || old_avc_config == nullptr || old_avc_config->Equals(new_avc_config) == false)
				{
					media_track->SetWidth(new_avc_config->GetWidth());
					media_track->SetHeight(new_avc_config->GetHeight());
					media_track->SetDecoderConfigurationRecord(new_avc_config);
				}
			}
			else
			{
				logtw("Failed to make AVCDecoderConfigurationRecord of %s/%s/%s track", stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
			}
		}

		// Insert SPS/PPS if there are no SPS/PPS nal units before IDR frame.
		if (has_idr == true && (has_sps == false || has_pps == false))
		{
			if (InsertH264SPSPPSAnnexB(stream_info, media_track, media_packet, !has_aud) == false)
			{
				logtw("Failed to insert SPS/PPS before IDR frame in %s/%s/%s track", stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
			}
		}
		else if (has_aud == false)
		{
			if (InsertH264AudAnnexB(stream_info, media_track, media_packet) == false)
			{
				logtw("Failed to insert AUD before IDR frame in %s/%s/%s track", stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
			}
		}

		return true;
	}

	return false;
}

bool MediaRouterNormalize::ProcessH264AnnexBStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	// Everytime : Make fragment header, Set keyframe flag, Append SPS/PPS nal units in front of IDR frame
	// one time : Parse track info from sps/pps and generate codec_extra_data
	std::shared_ptr<ov::Data> sps_nalu = nullptr, pps_nalu = nullptr;

	FragmentationHeader fragment_header;
	size_t offset = 0, offset_length = 0;
	auto bitstream = media_packet->GetData()->GetDataAs<uint8_t>();
	auto bitstream_length = media_packet->GetDataLength();
	bool has_sps = false, has_pps = false, has_idr = false, has_aud = false;

	while (offset < bitstream_length)
	{
		size_t start_code_size;
		int pos = 0;

		// Find Offset
		pos = H264Parser::FindAnnexBStartCode(bitstream + offset, bitstream_length - offset, start_code_size);
		if (pos == -1)
		{
			break;
		}

		offset += pos + start_code_size;

		// Find length and next offset
		pos = H264Parser::FindAnnexBStartCode(bitstream + offset, bitstream_length - offset, start_code_size);
		if (pos == -1)
		{
			// Last NALU
			offset_length = bitstream_length - offset;
		}
		else
		{
			offset_length = pos;
		}

		fragment_header.AddFragment(offset, offset_length);

		H264NalUnitHeader nal_header;
		if (H264Parser::ParseNalUnitHeader(bitstream + offset, offset_length, nal_header) == false)
		{
			logte("Could not parse H264 Nal unit header");
			return false;
		}

		// if (nal_header.GetNalUnitType() == H264NalUnitType::Sps || nal_header.GetNalUnitType() == H264NalUnitType::Pps)
		// {
		// 	logtt("NALU Type : %s", NalUnitTypeToStr(nal_header.GetNalUnitType()).CStr());
		// }

		if (nal_header.GetNalUnitType() == H264NalUnitType::Sps)
		{
			has_sps = true;

			if (sps_nalu == nullptr)
			{
				sps_nalu = std::make_shared<ov::Data>(bitstream + offset, offset_length);
			}
		}
		else if (nal_header.GetNalUnitType() == H264NalUnitType::Pps)
		{
			has_pps = true;

			if (pps_nalu == nullptr)
			{
				pps_nalu = std::make_shared<ov::Data>(bitstream + offset, offset_length);
			}
		}
		else if (nal_header.GetNalUnitType() == H264NalUnitType::IdrSlice)
		{
			has_idr = true;
			media_packet->SetFlag(MediaPacketFlag::Key);
		}
		else if (nal_header.GetNalUnitType() == H264NalUnitType::Aud)
		{
			has_aud = true;
		}
		else if (nal_header.GetNalUnitType() == H264NalUnitType::FillerData)
		{
			//TODO(Getroot): It is better to remove filler data.
		}

		// Last NalU
		if (pos == -1)
		{
			break;
		}

		offset += pos;
	}
	media_packet->SetFragHeader(&fragment_header);

	// Update DecoderConfigurationRecord
	if (sps_nalu != nullptr && pps_nalu != nullptr)
	{
		auto new_avc_config = std::make_shared<AVCDecoderConfigurationRecord>();

		new_avc_config->AddSPS(sps_nalu);
		new_avc_config->AddPPS(pps_nalu);

		if (new_avc_config->IsValid())
		{
			auto old_avc_config = std::static_pointer_cast<AVCDecoderConfigurationRecord>(media_track->GetDecoderConfigurationRecord());

			if (media_track->IsValid() == false || old_avc_config == nullptr || old_avc_config->Equals(new_avc_config) == false)
			{
				media_track->SetWidth(new_avc_config->GetWidth());
				media_track->SetHeight(new_avc_config->GetHeight());
				media_track->SetDecoderConfigurationRecord(new_avc_config);
			}
		}
		else
		{
			logtw("Failed to make AVCDecoderConfigurationRecord of %s/%s/%s track", stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
		}
	}

	// Insert SPS/PPS if there are no SPS/PPS nal units before IDR frame.
	if (has_idr == true && media_track->IsValid() && (has_sps == false || has_pps == false))
	{
		if (InsertH264SPSPPSAnnexB(stream_info, media_track, media_packet, !has_aud) == false)
		{
			logtw("Failed to insert SPS/PPS before IDR frame in %s/%s/%s track", stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
		}
	}
	else if (has_aud == false)
	{
		if (InsertH264AudAnnexB(stream_info, media_track, media_packet) == false)
		{
			logtw("Failed to insert AUD before IDR frame in %s/%s/%s track", stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
		}
	}

	return true;
}

bool MediaRouterNormalize::InsertH264SPSPPSAnnexB(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet, bool need_aud)
{
	if (media_track->IsValid() == false)
	{
		return false;
	}

	// Get AVC Decoder Configuration Record
	auto avc_config = std::static_pointer_cast<AVCDecoderConfigurationRecord>(media_track->GetDecoderConfigurationRecord());
	if (avc_config == nullptr)
	{
		return false;
	}

	auto data = media_packet->GetData();
	auto frag_header = media_packet->GetFragHeader();

	const uint8_t START_CODE[4] = {0x00, 0x00, 0x00, 0x01};
	const size_t START_CODE_LEN = sizeof(START_CODE);

	auto [sps_pps_annexb, sps_pps_frag_header] = avc_config->GetSpsPpsAsAnnexB(START_CODE_LEN, need_aud);
	if (sps_pps_annexb == nullptr)
	{
		return false;
	}

	// new media packet
	auto processed_data = std::make_shared<ov::Data>(data->GetLength() + 1024);

	// copy sps/pps first
	processed_data->Append(sps_pps_annexb);
	// and then copy original data
	processed_data->Append(data);

	// Update fragment header
	FragmentationHeader updated_frag_header;
	updated_frag_header.fragmentation_offset = sps_pps_frag_header.fragmentation_offset;
	updated_frag_header.fragmentation_length = sps_pps_frag_header.fragmentation_length;

	// Existing fragment header offset because SPS/PPS was inserted at front
	auto offset_offset = updated_frag_header.fragmentation_offset.back() + updated_frag_header.fragmentation_length.back();
	for (size_t i = 0; i < frag_header->fragmentation_offset.size(); i++)
	{
		size_t updated_offset = frag_header->fragmentation_offset[i] + offset_offset;
		updated_frag_header.AddFragment(updated_offset, frag_header->fragmentation_length[i]);
	}

	media_packet->SetFragHeader(&updated_frag_header);
	media_packet->SetData(processed_data);

	return true;
}

bool MediaRouterNormalize::InsertH264AudAnnexB(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	if (media_track->IsValid() == false)
	{
		return false;
	}

	auto data = media_packet->GetData();
	auto frag_header = media_packet->GetFragHeader();

	const uint8_t START_CODE[4] = {0x00, 0x00, 0x00, 0x01};
	const size_t START_CODE_LEN = sizeof(START_CODE);

	// new media packet
	auto processed_data = std::make_shared<ov::Data>(data->GetLength() + 1024);

	// copy AUD first
	const uint8_t AUD[2] = {0x09, 0xf0};
	processed_data->Append(START_CODE, sizeof(START_CODE));
	processed_data->Append(AUD, sizeof(AUD));

	// and then copy original data
	processed_data->Append(data);

	// Update fragment header
	FragmentationHeader updated_frag_header;
	updated_frag_header.AddFragment(START_CODE_LEN, sizeof(AUD));

	// Existing fragment header offset because AUD was inserted at front
	auto offset_offset = sizeof(AUD) + START_CODE_LEN;
	for (size_t i = 0; i < frag_header->fragmentation_offset.size(); i++)
	{
		size_t updated_offset = frag_header->fragmentation_offset[i] + offset_offset;
		updated_frag_header.AddFragment(updated_offset, frag_header->fragmentation_length[i]);
	}

	media_packet->SetFragHeader(&updated_frag_header);
	media_packet->SetData(processed_data);

	return true;
}

bool MediaRouterNormalize::ProcessAACRawStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	media_packet->SetFlag(MediaPacketFlag::Key);
	// everytime : Convert to ADTS
	// one time : Parse sequence header
	if (media_packet->GetPacketType() == cmn::PacketType::SEQUENCE_HEADER)
	{
		if (media_track->IsValid() == false)
		{
			// Validation
			auto audio_config = std::make_shared<AudioSpecificConfig>();
			if (audio_config->Parse(media_packet->GetData()) == false)
			{
				logte("aac sequence header paring error");
				return false;
			}

			media_track->SetSampleRate(audio_config->Samplerate());
			media_track->GetChannel().SetLayout(audio_config->Channel() == 1 ? AudioChannel::Layout::LayoutMono : AudioChannel::Layout::LayoutStereo);

			media_track->SetDecoderConfigurationRecord(audio_config);
		}

		return false;
	}
	else
	{
		if (media_track->IsValid() == false)
		{
			// Track information has not been parsed yet.
			logte("Raw aac sequence header has not parsed yet.");
			return false;
		}

		auto audio_config = std::static_pointer_cast<AudioSpecificConfig>(media_track->GetDecoderConfigurationRecord());
		if (audio_config == nullptr)
		{
			logte("Failed to get audio specific config.");
			return false;
		}

		// Convert to adts (raw aac data should be 1 frame)
		auto adts_data = AacConverter::ConvertRawToAdts(media_packet->GetData(), audio_config);
		if (adts_data == nullptr)
		{
			logte("Failed to convert raw aac to adts.");
			return false;
		}

		media_packet->SetData(adts_data);
		media_packet->SetBitstreamFormat(cmn::BitstreamFormat::AAC_ADTS);
		media_packet->SetPacketType(cmn::PacketType::RAW);
	}

	return true;
}

bool MediaRouterNormalize::ProcessAACAdtsStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	// One time : Parse track information

	media_packet->SetFlag(MediaPacketFlag::Key);

	// AAC ADTS only needs to analyze the track information of the media stream once.
	if (media_track->IsValid() == true)
	{
		return true;
	}

	// Make AudioSpecificConfig from ADTS Header
	AACAdts adts;
	if (AACAdts::Parse(media_packet->GetData()->GetDataAs<uint8_t>(), media_packet->GetDataLength(), adts) == false)
	{
		logte("Could not parse AAC ADTS header");
		return false;
	}

	auto audio_config = std::make_shared<AudioSpecificConfig>();
	audio_config->SetObjectType(adts.ObjectType());
	audio_config->SetSamplingFrequencyIndex(adts.SamplingFrequencyIndex());
	audio_config->SetChannel(adts.ChannelConfiguration());

	media_track->SetSampleRate(audio_config->Samplerate());
	media_track->GetChannel().SetLayout(audio_config->Channel() == 1 ? AudioChannel::Layout::LayoutMono : AudioChannel::Layout::LayoutStereo);

	media_track->SetDecoderConfigurationRecord(audio_config);

	return true;
}

bool MediaRouterNormalize::ProcessH265AnnexBStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	// Everytime : Generate fragmentation header, Check key frame
	// One time : Parse SPS and Set width/height (track information)

	// TODO : Append SPS/PPS nal units in front of IDR frame

	std::shared_ptr<HEVCDecoderConfigurationRecord> hevc_config = nullptr;
	FragmentationHeader fragment_header;

	auto bitstream = media_packet->GetData()->GetDataAs<uint8_t>();
	auto bitstream_length = media_packet->GetDataLength();
	bool has_vps = false, has_sps = false, has_pps = false, has_idr = false;

	size_t offset = 0, offset_length = 0;
	while (offset < bitstream_length)
	{
		size_t start_code_size;
		int pos = 0;

		// Find Offset
		pos = H265Parser::FindAnnexBStartCode(bitstream + offset, bitstream_length - offset, start_code_size);
		if (pos == -1)
		{
			break;
		}

		offset += pos + start_code_size;

		// Find length and next offset
		pos = H265Parser::FindAnnexBStartCode(bitstream + offset, bitstream_length - offset, start_code_size);
		if (pos == -1)
		{
			// Last NALU
			offset_length = bitstream_length - offset;
		}
		else
		{
			offset_length = pos;
		}

		fragment_header.AddFragment(offset, offset_length);

		H265NalUnitHeader header;
		if (H265Parser::ParseNalUnitHeader(bitstream + offset, H265_NAL_UNIT_HEADER_SIZE, header) == false)
		{
			logte("Could not parse H265 Nal unit header");
			return false;
		}

		// Key Frame
		if (IsH265KeyFrame(header.GetNalUnitType()))
		{
			media_packet->SetFlag(MediaPacketFlag::Key);

			has_idr = true;
		}
		// VPS/SPS/PPS
		else if (header.GetNalUnitType() == H265NALUnitType::VPS)
		{
			has_vps = true;
		}
		else if (header.GetNalUnitType() == H265NALUnitType::SPS)
		{
			has_sps = true;
		}
		else if (header.GetNalUnitType() == H265NALUnitType::PPS)
		{
			has_pps = true;
		}

		// Track info
		if (media_track->IsValid() == false)
		{
			if (hevc_config == nullptr)
			{
				hevc_config = std::make_shared<HEVCDecoderConfigurationRecord>();
			}

			if (header.GetNalUnitType() == H265NALUnitType::VPS ||
				header.GetNalUnitType() == H265NALUnitType::SPS ||
				header.GetNalUnitType() == H265NALUnitType::PPS)
			{
				auto nal_unit = std::make_shared<ov::Data>(bitstream + offset, offset_length);
				hevc_config->AddNalUnit(header.GetNalUnitType(), nal_unit);
			}
		}

		// Last NalU
		if (pos == -1)
		{
			break;
		}

		offset += pos;
	}
	media_packet->SetFragHeader(&fragment_header);

	// Update DecoderConfigurationRecord
	if (hevc_config && hevc_config->IsValid())
	{
		auto old_hevc_config = std::static_pointer_cast<HEVCDecoderConfigurationRecord>(media_track->GetDecoderConfigurationRecord());

		if (old_hevc_config == nullptr || old_hevc_config->Equals(hevc_config) == false)
		{
			media_track->SetWidth(hevc_config->GetWidth());
			media_track->SetHeight(hevc_config->GetHeight());
			media_track->SetDecoderConfigurationRecord(hevc_config);
		}
	}

	// Insert VPS/SPS/PPS if there are no VPS/SPS/PPS nal units before IDR frame.
	if (media_track->IsValid() && has_idr && ((has_vps == false) || (has_sps == false) || (has_pps == false)))
	{
		hevc_config = std::static_pointer_cast<HEVCDecoderConfigurationRecord>(media_track->GetDecoderConfigurationRecord());

		if (hevc_config != nullptr)
		{
			auto current_data = media_packet->GetData();
			auto new_data = std::make_shared<ov::Data>(current_data->GetLength() + 1024);
			FragmentationHeader new_fragment_header;

			if (hevc_config->AddVpsSpsPpsAnnexB(new_data, &new_fragment_header) == false)
			{
				logtw("Failed to insert VPS/SPS/PPS before IDR frame in %s/%s/%s track", stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
			}

			new_data->Append(current_data);
			media_packet->SetData(new_data);

			new_fragment_header.AddFragments(media_packet->GetFragHeader());
			media_packet->SetFragHeader(&new_fragment_header);
		}
		else
		{
			logte("[%s/%s/%s] HEVCDecoderConfigurationRecord is not set",
				  stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
		}
	}

	return true;
}

bool MediaRouterNormalize::ProcessH265HVCCStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	if (media_packet->GetBitstreamFormat() != cmn::BitstreamFormat::HVCC)
	{
		return false;
	}

	if (media_packet->GetPacketType() == cmn::PacketType::SEQUENCE_HEADER)
	{
		// Validation
		auto hevc_config = std::make_shared<HEVCDecoderConfigurationRecord>();

		if (hevc_config->Parse(media_packet->GetData()) == false)
		{
			logte("Could not parse sequence header");
			return false;
		}

		media_track->SetWidth(hevc_config->GetWidth());
		media_track->SetHeight(hevc_config->GetHeight());

		media_track->SetDecoderConfigurationRecord(hevc_config);

		return false;
	}

	// Convert to AnnexB and Insert SPS/PPS if there are no SPS/PPS nal units.
	else if (media_packet->GetPacketType() == cmn::PacketType::NALU)
	{
		FragmentationHeader fragment_header;
		size_t nalu_offset = 0;

		std::shared_ptr<ov::Data> vps_nalu = nullptr;
		std::shared_ptr<ov::Data> sps_nalu = nullptr;
		std::shared_ptr<ov::Data> pps_nalu = nullptr;

		bool has_idr = false;
		bool has_aud = false;

		ov::ByteStream read_stream(media_packet->GetData());

		media_packet->SetBitstreamFormat(cmn::BitstreamFormat::H265_ANNEXB);

		// To minimize data copying, we store the data separately and aggregate it later
		std::vector<std::shared_ptr<const ov::Data>> nalu_list;
		size_t nalu_data_size = 0;

		while (read_stream.Remained() > 0)
		{
			if (read_stream.IsRemained(4) == false)
			{
				logte("Not enough data to parse NAL");
				return false;
			}

			size_t nal_length = read_stream.ReadBE32();
			if (read_stream.IsRemained(nal_length) == false)
			{
				logte("NAL length (%d) is greater than buffer length (%d)", nal_length, read_stream.Remained());
				return false;
			}

			auto nalu = read_stream.GetRemainData(nal_length);

			// Exception handling for encoder that transmits HVCC in non-standard format ([Size][Start Code][NalU])
			{
				auto start_code_size = GetStartCodeSize(nalu->GetDataAs<uint8_t>(), nalu->GetLength());
				if (start_code_size > 0)
				{
					read_stream.Skip(start_code_size);
					nal_length -= start_code_size;
					nalu = nalu->Subdata(start_code_size, nal_length);
				}
			}

			[[maybe_unused]] auto skipped = read_stream.Skip(nal_length);
			OV_ASSERT2(skipped == nal_length);

			H265NalUnitHeader nal_header;
			if (H265Parser::ParseNalUnitHeader(nalu, nal_header))
			{
				const auto nalu_type = nal_header.GetNalUnitType();

				has_idr = IsH265KeyFrame(nalu_type);

				if (has_idr)
				{
					media_packet->SetFlag(MediaPacketFlag::Key);
				}
				else if (nalu_type == H265NALUnitType::VPS)
				{
					vps_nalu = (vps_nalu != nullptr) ? vps_nalu : nalu->Clone();
				}
				else if (nalu_type == H265NALUnitType::SPS)
				{
					sps_nalu = (sps_nalu != nullptr) ? sps_nalu : nalu->Clone();
				}
				else if (nalu_type == H265NALUnitType::PPS)
				{
					pps_nalu = (pps_nalu != nullptr) ? pps_nalu : nalu->Clone();
				}
				else if (nalu_type == H265NALUnitType::AUD)
				{
					has_aud = true;
				}
			}
			else
			{
				logte("Could not parse H265 Nal unit header");
				OV_ASSERT2(false);
				return false;
			}

			nalu_offset += H26X_START_CODE_PREFIX_LEN;
			fragment_header.AddFragment(nalu_offset, nal_length);

			nalu_offset += nal_length;

			nalu_list.push_back(nalu);
			nalu_data_size += nal_length;
		}

		// Update DecoderConfigurationRecord
		if ((vps_nalu != nullptr) && (sps_nalu != nullptr) && (pps_nalu != nullptr))
		{
			auto new_hevc_config = std::make_shared<HEVCDecoderConfigurationRecord>();

			new_hevc_config->AddNalUnit(H265NALUnitType::VPS, vps_nalu);
			new_hevc_config->AddNalUnit(H265NALUnitType::SPS, sps_nalu);
			new_hevc_config->AddNalUnit(H265NALUnitType::PPS, pps_nalu);

			if (new_hevc_config->IsValid())
			{
				auto old_hevc_config = media_track->GetDecoderConfigurationRecordAs<HEVCDecoderConfigurationRecord>();

				if ((media_track->IsValid() == false) || (new_hevc_config->Equals(old_hevc_config) == false))
				{
					media_track->SetWidth(new_hevc_config->GetWidth());
					media_track->SetHeight(new_hevc_config->GetHeight());
					media_track->SetDecoderConfigurationRecord(new_hevc_config);
				}
			}
			else
			{
				logtw("[%s/%s/%s] Failed to make HEVCDecoderConfigurationRecord",
					  stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
			}
		}

		const bool need_to_add_aud = (has_aud == false);
		const bool need_to_add_vps_sps_pps = has_idr && ((vps_nalu == nullptr) || (sps_nalu == nullptr) || (pps_nalu == nullptr));

		std::shared_ptr<ov::Data> additional_data = nullptr;

		if (media_track->IsValid() && (need_to_add_aud || need_to_add_vps_sps_pps))
		{
			auto hevc_config = media_track->GetDecoderConfigurationRecordAs<HEVCDecoderConfigurationRecord>();

			if (hevc_config != nullptr)
			{
				FragmentationHeader additional_fragment_header;
				additional_data = std::make_shared<ov::Data>(1024);

				// Insert AUD if there is no AUD nal unit
				if (need_to_add_aud)
				{
					if (hevc_config->AddAudAnnexB(additional_data, &additional_fragment_header) == false)
					{
						logtw("[%s/%s/%s] Failed to insert AUD before frame",
							  stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
					}
				}

				// Insert VPS/SPS/PPS if there are no VPS/SPS/PPS nal units before IDR frame
				if (need_to_add_vps_sps_pps)
				{
					if (hevc_config->AddVpsSpsPpsAnnexB(additional_data, &additional_fragment_header) == false)
					{
						logtw("[%s/%s/%s] Failed to insert VPS/SPS/PPS before IDR frame",
							  stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
					}
				}

				fragment_header.InsertFragments(&additional_fragment_header);
			}
			else
			{
				logte("[%s/%s/%s] HEVCDecoderConfigurationRecord is not set",
					  stream_info->GetApplicationName(), stream_info->GetName().CStr(), media_track->GetVariantName().CStr());
			}
		}

		// Copy `additional_data` to `converted_data` if it exists, otherwise create a new one
		size_t expected_size =
			// Size of start code prefix for each NALU
			(H26X_START_CODE_PREFIX_LEN * nalu_list.size()) +
			// Total NALU data size
			nalu_data_size;
		std::shared_ptr<ov::Data> annexb_data;

		if (additional_data != nullptr)
		{
			expected_size += additional_data->GetLength();
			// Since `additional_data` is newly created above, we can use it directly without `Clone()`
			annexb_data = additional_data;
			annexb_data->Reserve(expected_size);
		}
		else
		{
			annexb_data = std::make_shared<ov::Data>(expected_size);
		}

		// Append NALUs with start code prefix to `annexb_data`
		for (const auto &nalu : nalu_list)
		{
			// Append start code prefix
			annexb_data->Append(H26X_START_CODE_PREFIX, H26X_START_CODE_PREFIX_LEN);
			// Append NALU data
			annexb_data->Append(nalu);
		}

		OV_ASSERT(annexb_data->GetLength() == expected_size, "AnnexB data size mismatch: expected %zu, got %zu", expected_size, annexb_data->GetLength());

		media_packet->SetFragHeader(&fragment_header);
		media_packet->SetData(annexb_data);
	}

	return true;
}

bool MediaRouterNormalize::ProcessVP8Stream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	// One time : parse width, height
	if (media_track->IsValid() == true)
	{
		return true;
	}

	VP8Parser parser;
	if (VP8Parser::Parse(media_packet->GetData()->GetDataAs<uint8_t>(), media_packet->GetDataLength(), parser) == false)
	{
		logte("Could not parse VP8 header");
		return false;
	}

	media_track->SetWidth(parser.GetWidth());
	media_track->SetHeight(parser.GetHeight());

	// TODO(Getroot) : In VP8, there is no need to know whether it is the current keyframe. So it doesn't parse every time.
	// However, if this is needed in the future, VP8Parser writes and applies a low-cost code that only determines whether or not it is a keyframe.
	if (parser.IsKeyFrame())
	{
		media_packet->SetFlag(MediaPacketFlag::Key);
	}

	return true;
}

bool MediaRouterNormalize::ProcessOPUSStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	// One time : parse samplerate, channel
	if (media_track->IsValid() == true)
	{
		return true;
	}

	OPUSParser parser;
	if (OPUSParser::Parse(media_packet->GetData()->GetDataAs<uint8_t>(), media_packet->GetDataLength(), parser) == false)
	{
		logte("Could not parse OPUS header");
		return false;
	}

	// The opus has a fixed samplerate of 48000
	media_track->SetSampleRate(48000);
	media_track->GetChannel().SetLayout((parser.GetStereoFlag() == 0) ? (AudioChannel::Layout::LayoutMono) : (AudioChannel::Layout::LayoutStereo));

	return true;
}

bool MediaRouterNormalize::ProcessMP3Stream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet)
{
	// One time : parse samplerate, channel
	if (media_track->IsValid() == true)
	{
		return true;
	}

	MP3Parser parser;
	if (MP3Parser::Parse(media_packet->GetData()->GetDataAs<uint8_t>(), media_packet->GetDataLength(), parser) == false)
	{
		logte("Could not parse MP3 header");
		return false;
	}

	logti("MP3Parser : %s", parser.GetInfoString().CStr());

	media_track->SetSampleRate(parser.GetSampleRate());
	media_track->SetBitrateByMeasured(parser.GetBitrate());
	media_track->GetChannel().SetLayout((parser.GetChannelCount() == 1) ? (AudioChannel::Layout::LayoutMono) : (AudioChannel::Layout::LayoutStereo));

	return true;
}
