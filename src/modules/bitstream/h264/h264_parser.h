#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/nalu/nal_unit_bitstream_parser.h>
#include <stdint.h>

#include "h264_common.h"

#define H264_NAL_UNIT_HEADER_SIZE 1

class AVCDecoderConfigurationRecord;
class H264NalUnitHeader
{
public:
	H264NalUnitType GetNalUnitType()
	{
		return _type;
	}

	bool IsVideoSlice() const
	{
		return _type >= H264NalUnitType::NonIdrSlice && _type <= H264NalUnitType::IdrSlice;
	}

private:
	uint8_t _nal_ref_idc = 0;
	H264NalUnitType _type = H264NalUnitType::Unspecified;

	friend class H264Parser;
};

class H264SPS
{
	struct ASPECT_RATIO
	{
		uint16_t _width;
		uint16_t _height;
	};

public:
	unsigned int GetWidth() const
	{
		return _width;
	}
	unsigned int GetHeight() const
	{
		return _height;
	}
	uint8_t GetProfileIdc() const
	{
		return _profile;
	}
	uint8_t GetConstraintFlag() const
	{
		return _constraint;
	}
	uint8_t GetCodecLevelIdc() const
	{
		return _codec_level;
	}
	unsigned int GetFps() const
	{
		return _fps;
	}
	unsigned int GetId() const
	{
		return _id;
	}
	unsigned int GetMaxNrOfReferenceFrames() const
	{
		return _max_nr_of_reference_frames;
	}

	// ITU-T H.264 E.2.1 / E.2.2
	// These are required to read or write a pic_timing SEI (payloadType 1), because the
	// presence and the bit width of its fields are all defined by the SPS VUI.
	bool IsCpbDpbDelaysPresent() const
	{
		return (_nal_hrd_parameters_present_flag != 0) || (_vcl_hrd_parameters_present_flag != 0);
	}
	uint8_t GetCpbRemovalDelayLength() const
	{
		return _cpb_removal_delay_length_minus1 + 1;
	}
	uint8_t GetDpbOutputDelayLength() const
	{
		return _dpb_output_delay_length_minus1 + 1;
	}
	uint8_t GetTimeOffsetLength() const
	{
		return _time_offset_length;
	}
	bool IsPicStructPresent() const
	{
		return _pic_struct_present_flag != 0;
	}

	// A stream with frame_mbs_only_flag set cannot carry fields, so every picture is a
	// progressive frame (ITU-T H.264 7.4.2.1.1)
	bool IsFrameMbsOnly() const
	{
		return _frame_mbs_only_flag != 0;
	}
	// The fields a slice_header() has to be walked through to reach field_pic_flag
	uint8_t GetLog2MaxFrameNum() const
	{
		return static_cast<uint8_t>(_log2_max_frame_num_minus4 + 4);
	}
	bool IsSeparateColourPlane() const
	{
		return _separate_colour_plane_flag != 0;
	}

	// Bit position of pic_struct_present_flag in the buffer given to ParseSPS, escape bytes
	// included, so that buffer has to be the NAL as it appears in the bitstream. 0 means the flag
	// is not coded (no VUI). See H264SpsRewriter.
	size_t GetPicStructPresentFlagBitPos() const
	{
		return _pic_struct_present_flag_bit_pos;
	}

	ov::String GetInfoString()
	{
		ov::String out_str = ov::String::FormatString("\n[H264Sps]\n");

		out_str.AppendFormat("\tProfile(%d)\n", GetProfileIdc());
		out_str.AppendFormat("\tCodecLevel(%d)\n", GetCodecLevelIdc());
		out_str.AppendFormat("\tWidth(%d)\n", GetWidth());
		out_str.AppendFormat("\tHeight(%d)\n", GetHeight());
		out_str.AppendFormat("\tFps(%d)\n", GetFps());
		out_str.AppendFormat("\tId(%d)\n", GetId());
		out_str.AppendFormat("\tMaxNrOfReferenceFrames(%d)\n", GetMaxNrOfReferenceFrames());
		out_str.AppendFormat("\tAspectRatio(IDC - %d Extended - %d:%d)\n", _aspect_ratio_idc, _aspect_ratio._width, _aspect_ratio._height);

		return out_str;
	}

private:
	uint8_t _profile = 0;
	uint8_t _constraint = 0;
	uint8_t _codec_level = 0;
	unsigned int _width = 0;
	unsigned int _height = 0;
	unsigned int _fps = 0;
	unsigned int _id = 0;
    uint32_t _chroma_format_idc = 1;
	unsigned int _max_nr_of_reference_frames = 0;
	uint8_t _aspect_ratio_idc = 0;
	ASPECT_RATIO _aspect_ratio = {0, 0};

	uint32_t _log2_max_frame_num_minus4 = 0;
	uint8_t _frame_mbs_only_flag = 0;
	uint8_t _separate_colour_plane_flag = 0;
	uint32_t _pic_order_cnt_type = 0;
	uint32_t _log2_max_pic_order_cnt_lsb_minus4 = 0;
	uint8_t _delta_pic_order_always_zero_flag = 0;

	// VUI parameters related to pic_timing SEI (ITU-T H.264 E.1.1, E.1.2)
	uint8_t _nal_hrd_parameters_present_flag = 0;
	uint8_t _vcl_hrd_parameters_present_flag = 0;
	uint8_t _pic_struct_present_flag = 0;
	// hrd_parameters() lengths. When no hrd_parameters() is present, time_offset_length is
	// inferred to be 24 and the delay lengths are meaningless (CpbDpbDelaysPresentFlag is 0).
	uint8_t _cpb_removal_delay_length_minus1 = 23;
	uint8_t _dpb_output_delay_length_minus1 = 23;
	uint8_t _time_offset_length = 24;
	size_t _pic_struct_present_flag_bit_pos = 0;

	friend class H264Parser;
};

// ITU-T H.264 7.3.2.2
class H264PPS
{
public:
	unsigned int GetId() const
	{
		return _pps_id;
	}

	unsigned int GetSpsId() const
	{
		return _sps_id;
	}

private:
	uint32_t _pps_id = 0;
	uint32_t _sps_id = 0;

    uint32_t _num_slice_groups_minus1 = 0;
    uint8_t _deblocking_filter_control_present_flag = 0;
    bool _entropy_coding_mode_flag = false;
	bool _bottom_field_pic_order_in_frame_present_flag = false;
	uint8_t _redundant_pic_cnt_present_flag = 0;
    uint32_t _num_ref_idx_l0_default_active_minus1 = 0;
    uint32_t _num_ref_idx_l1_default_active_minus1 = 0;

    uint8_t _weighted_pred_flag = 0;
    uint8_t _weighted_bipred_idc = 0;

	friend class H264Parser;
};

// ITU-T H.264 7.3.3
class H264SliceHeader
{
public:
	enum class SliceType : uint8_t
	{
		PSlice = 0,
		BSlice = 1,
		ISlice = 2,
		SPSlice = 3,
		SISlice = 4
	};

    SliceType GetSliceType() const
    {
        return static_cast<SliceType>(_slice_type % 5);
    }

    size_t GetHeaderSizeInBits() const
    {
        return _header_size_in_bits;
    }

    size_t GetHeaderSizeInBytes() const
    {
        return ((_header_size_in_bits + 7) / 8);
    }

private:
	uint32_t _slice_type;

    uint32_t _num_ref_idx_l0_active_minus1 = 0;
    uint32_t _num_ref_idx_l1_active_minus1 = 0;

    size_t _header_size_in_bits = 0;

	friend class H264Parser;
};

struct NaluIndex
{
	size_t _start_offset;
	size_t _payload_offset;
	size_t _payload_size;
};

// H264 Bitstream Parser Utility
class H264Parser
{
public:

	static std::vector<NaluIndex> FindNaluIndexes(const uint8_t *bitstream, size_t length);
	// returns offset (start point), code_size : 3(001) or 4(0001)
	// returns -1 if there is no start code in the buffer
	static int FindAnnexBStartCode(const uint8_t *bitstream, size_t length, size_t &code_size);
	static bool CheckAnnexBKeyframe(const uint8_t *bitstream, size_t length);
	static bool ParseNalUnitHeader(const uint8_t *nalu, size_t length, H264NalUnitHeader &header);
	
    // SPS Parsers
    static bool ParseSPS(const uint8_t *nalu, size_t length, H264SPS &sps);
    static bool ParseVUI(NalUnitBitstreamParser &parser, H264SPS &sps);
    static bool ParseHrdParameters(NalUnitBitstreamParser &parser, H264SPS &sps);

    // PPS Parsers
	static bool ParsePPS(const uint8_t *nalu, size_t length, H264PPS &pps);
	

    // Slice Header Parsers
	static bool ParseSliceHeader(const uint8_t *nalu, size_t length, H264SliceHeader &header, std::shared_ptr<AVCDecoderConfigurationRecord> &avcc);
    static bool ParseRefPicListReordering(NalUnitBitstreamParser &parser, H264SliceHeader &header);
    static bool ParsePredWeightTable(NalUnitBitstreamParser &parser, const H264SPS &sps, H264SliceHeader &header);
    static bool ParseDecRefPicMarking(NalUnitBitstreamParser &parser, bool  idr, H264SliceHeader &header);

	static bool ParseNalUnitHeader(NalUnitBitstreamParser &parser, H264NalUnitHeader &header);
};
