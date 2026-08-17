// I developed the code of this file by referring to the source code of virinext's hevcsbrowser (https://github.com/virinext/hevcesbrowser).
// Thanks to virinext.
// - Getroot
#pragma once

#include <base/ovlibrary/ovlibrary.h>

// Rec. ITU-T H.265 (V10), Table 7-1 – NAL unit type codes and NAL unit type classes
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | nal_unit_type | Name of nal_unit_type | Content of NAL unit nad RBSP syntax structure                 | NAL unit type class   |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 0             | TRAIL_N               | Coded slice segment of a non-TSA, non-STSA trailing picture   | VCL                   |
// | 1             | TRAIL_R               | slice_segment_layer_rbsp( )                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 2             | TSA_N                 | Coded slice segment of a TSA picture                          | VCL                   |
// | 3             | TSA_R                 | slice_segment_layer_rbsp( )                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 4             | STSA_N                | Coded slice segment of an STSA picture                        | VCL                   |
// | 5             | STSA_R                | slice_segment_layer_rbsp( )                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 6             | RADL_N                | Coded slice segment of a RADL picture                         | VCL                   |
// | 7             | RADL_R                | slice_segment_layer_rbsp( )                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 8             | RASL_N                | Coded slice segment of a RASL picture                         | VCL                   |
// | 9             | RASL_R                | slice_segment_layer_rbsp( )                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 10            | RSV_VCL_N10           | Reserved non-IRAP SLNR VCL NAL unit types VCL                 | VCL                   |
// | 12            | RSV_VCL_N12           |                                                               |                       |
// | 14            | RSV_VCL_N14           |                                                               |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 11            | RSV_VCL_R11           | Reserved non-IRAP sub-layer reference VCL NAL unit types      | VCL                   |
// | 13            | RSV_VCL_R13           |                                                               |                       |
// | 15            | RSV_VCL_R15           |                                                               |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 16            | BLA_W_LP              | Coded slice segment of a BLA picture                          | VCL                   |
// | 17            | BLA_W_RADL            | slice_segment_layer_rbsp( )                                   |                       |
// | 18            | BLA_N_LP              |                                                               |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 19            | IDR_W_RADL            | Coded slice segment of an IDR picture                         | VCL                   |
// | 20            | IDR_N_LP              | slice_segment_layer_rbsp( )                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 21            | CRA_NUT               | Coded slice segment of a CRA picture                          | VCL                   |
// |               |                       | slice_segment_layer_rbsp( )                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 22            | RSV_IRAP_VCL22        | Reserved IRAP VCL NAL unit types VCL                          | VCL                   |
// | 23            | RSV_IRAP_VCL23        |                                                               |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 24..31        | RSV_VCL24..           | Reserved non-IRAP VCL NAL unit types VCL                      | VCL                   |
// |               | RSV_VCL31             |                                                               |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 32            | VPS_NUT               | Video parameter set                                           | non-VCL               |
// |               |                       | video_parameter_set_rbsp( )                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 33            | SPS_NUT               | Sequence parameter set                                        | non-VCL               |
// |               |                       | seq_parameter_set_rbsp( )                                     |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 34            | PPS_NUT               | Picture parameter set                                         | non-VCL               |
// |               |                       | pic_parameter_set_rbsp( )                                     |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 35            | AUD_NUT               | Access unit delimiter                                         | non-VCL               |
// |               |                       | access_unit_delimiter_rbsp( )                                 |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 36            | EOS_NUT               | End of sequence                                               | non-VCL               |
// |               |                       | end_of_seq_rbsp( )                                            |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 37            | EOB_NUT               | End of bitstream                                              | non-VCL               |
// |               |                       | end_of_bitstream_rbsp( )                                      |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 38            | FD_NUT                | Filler data                                                   | non-VCL               |
// |               |                       | filler_data_rbsp( )                                           |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 39            | PREFIX_SEI_NUT        | Supplemental enhancement information                          | non-VCL               |
// | 40            | SUFFIX_SEI_NUT        | sei_rbsp( )                                                   |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 41..47        | RSV_NVCL41..          | Reserved                                                      | non-VCL               |
// |               | RSV_NVCL47            |                                                               |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
// | 48..63        | UNSPEC48..            | Unspecified                                                   | non-VCL               |
// |               | UNSPEC63              |                                                               |                       |
// +---------------+-----------------------+---------------------------------------------------------------+-----------------------+
enum class H265NALUnitType : uint8_t
{
	TRAIL_N = 0,
	TRAIL_R = 1,
	TSA_N = 2,
	TSA_R = 3,
	STSA_N = 4,
	STSA_R = 5,
	RADL_N = 6,
	RADL_R = 7,
	RASL_N = 8,
	RASL_R = 9,
	RSV_VCL_N10 = 10,
	RSV_VCL_N12 = 12,
	RSV_VCL_N14 = 14,
	RSV_VCL_R11 = 11,
	RSV_VCL_R13 = 13,
	RSV_VCL_R15 = 15,
	BLA_W_LP = 16,
	BLA_W_RADL = 17,
	BLA_N_LP = 18,
	IDR_W_RADL = 19,
	IDR_N_LP = 20,
	CRA_NUT = 21,
	RSV_IRAP_VCL22 = 22,
	RSV_IRAP_VCL23 = 23,
	RSV_VCL24 = 24,
	RSV_VCL25 = 25,
	RSV_VCL26 = 26,
	RSV_VCL27 = 27,
	RSV_VCL28 = 28,
	RSV_VCL29 = 29,
	RSV_VCL30 = 30,
	RSV_VCL31 = 31,
	VPS = 32,  // VPS_NUT
	SPS = 33,  // SPS_NUT
	PPS = 34,  // PPS_NUT
	AUD = 35,  // AUD_NUT
	EOS_NUT = 36,
	EOB_NUT = 37,
	FD_NUT = 38,
	PREFIX_SEI_NUT = 39,
	SUFFIX_SEI_NUT = 40,
	RSV_NVCL41 = 41,
	RSV_NVCL42 = 42,
	RSV_NVCL43 = 43,
	RSV_NVCL44 = 44,
	RSV_NVCL45 = 45,
	RSV_NVCL46 = 46,
	RSV_NVCL47 = 47,
	UNSPEC48 = 48,
	UNSPEC49 = 49,
	UNSPEC50 = 50,
	UNSPEC51 = 51,
	UNSPEC52 = 52,
	UNSPEC53 = 53,
	UNSPEC54 = 54,
	UNSPEC55 = 55,
	UNSPEC56 = 56,
	UNSPEC57 = 57,
	UNSPEC58 = 58,
	UNSPEC59 = 59,
	UNSPEC60 = 60,
	UNSPEC61 = 61,
	UNSPEC62 = 62,
	UNSPEC63 = 63,
};
constexpr const char *EnumToString(H265NALUnitType nal_unit_type)
{
	switch (nal_unit_type)
	{
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, TRAIL_N);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, TRAIL_R);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, TSA_N);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, TSA_R);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, STSA_N);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, STSA_R);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RADL_N);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RADL_R);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RASL_N);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RASL_R);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL_N10);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL_N12);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL_N14);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL_R11);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL_R13);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL_R15);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, BLA_W_LP);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, BLA_W_RADL);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, BLA_N_LP);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, IDR_W_RADL);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, IDR_N_LP);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, CRA_NUT);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_IRAP_VCL22);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_IRAP_VCL23);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL24);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL25);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL26);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL27);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL28);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL29);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL30);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_VCL31);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, VPS);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, SPS);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, PPS);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, AUD);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, EOS_NUT);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, EOB_NUT);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, FD_NUT);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, PREFIX_SEI_NUT);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, SUFFIX_SEI_NUT);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_NVCL41);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_NVCL42);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_NVCL43);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_NVCL44);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_NVCL45);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_NVCL46);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, RSV_NVCL47);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC48);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC49);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC50);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC51);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC52);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC53);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC54);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC55);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC56);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC57);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC58);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC59);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC60);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC61);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC62);
		OV_CASE_RETURN_ENUM_STRING(H265NALUnitType, UNSPEC63);
	}

	return "(Unknown)";
}
bool IsH265KeyFrame(H265NALUnitType nal_unit_type);

bool operator==(uint8_t first, H265NALUnitType second);
bool operator!=(uint8_t first, H265NALUnitType second);

constexpr const uint8_t H26X_START_CODE_PREFIX[4] = {0x00, 0x00, 0x00, 0x01};
constexpr const size_t H26X_START_CODE_PREFIX_LEN = sizeof(H26X_START_CODE_PREFIX);

constexpr uint8_t H265_AUD[3] = {
	//
	// First byte (NALU header)
	//
	// forbidden_zero_bit (1bit) | // nal_unit_type (6bits) | nuh_layer_id (1bit of 6bits)
	(0b0 << 7) | (ov::ToUnderlyingType(H265NALUnitType::AUD) << 1) | 0b0,
	//
	// Second byte (NALU header)
	//
	// nuh_layer_id (5bits of 6bits) | nuh_temporal_id_plus1 (3bits)
	0b00000 << 3 | 0b0001,
	//
	// Third byte (AUD payload)
	//
	// pic_type (3bits) | rbsp_trailing_bit (5bits)
	0b000 << 5 | 0b10000};

static constexpr auto H265_AUD_SIZE = sizeof(H265_AUD);
