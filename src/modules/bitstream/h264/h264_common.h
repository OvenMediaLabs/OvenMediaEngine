#pragma once

#include <base/ovlibrary/ovlibrary.h>

constexpr auto kH264NalUnitTypeMask = 0b11111;
constexpr auto kH264NalRefIdcMask = 0b1100000;

#define GET_NAL_REF_IDC(x) (((x) & kH264NalRefIdcMask) >> 5)

constexpr auto kH264SpsPpsNalRefIdc = 0b11;

enum class H264SliceType : uint8_t
{
    P = 0,
    B = 1,
    I = 2,
    Sp = 3,
    Si = 4
};

int16_t GetSliceTypeNalRefIdc(uint8_t slice_type);

// If this enumeration is updated, also update IsKnownH264NalUnitType
enum class H264NalUnitType : uint8_t
{
    Unspecified = 0,
    NonIdrSlice = 1,
    DPA = 2, // CodedSliceDataPartitionA
    DPB = 3, // CodedSliceDataPartitionB
    DPC = 4, // CodedSliceDataPartitionC
    IdrSlice = 5,
    Sei = 6,
    Sps = 7,
    Pps = 8,
    Aud = 9,
    EndOfSequence = 10,
    EndOfStream = 11,
    FillerData = 12,
    SpsExt = 13,
    AuxiliarySlice = 19
};

constexpr const char *NalUnitTypeToStr(H264NalUnitType nal_unit_type);
bool IsValidH264NalUnitType(uint8_t nal_unit_type);
bool IsKnownH264NalUnitType(H264NalUnitType nal_unit_type);

bool operator==(uint8_t first, H264NalUnitType second);
bool operator!=(uint8_t first, H264NalUnitType second);
