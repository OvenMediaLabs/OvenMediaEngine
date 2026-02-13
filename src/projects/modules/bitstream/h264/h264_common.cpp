#include "h264_common.h"

#define OV_LOG_TAG "H264NalUnitTypes"

int16_t GetSliceTypeNalRefIdc(uint8_t slice_type)
{
	if (5 <= slice_type && slice_type <= 9)
	{
		slice_type %= 5;
	}

	switch (static_cast<H264SliceType>(slice_type))
	{
		case H264SliceType::P:
			return 0b10;

		case H264SliceType::I:
			return 0b11;

		case H264SliceType::B:
			[[fallthrough]];
		case H264SliceType::Sp:
			[[fallthrough]];
		case H264SliceType::Si:
			break;
	}

	return -1;
}

bool IsValidH264NalUnitType(uint8_t nal_unit_type)
{
#if defined(DEBUG)
	IsKnownH264NalUnitType(static_cast<H264NalUnitType>(nal_unit_type));
#endif
	return (0 < nal_unit_type && nal_unit_type < 16) || (18 < nal_unit_type && nal_unit_type < 21);
}

bool IsKnownH264NalUnitType(H264NalUnitType nal_unit_type)
{
	switch (nal_unit_type)
	{
		case H264NalUnitType::Unspecified:
			break;

		case H264NalUnitType::NonIdrSlice:
			[[fallthrough]];
		case H264NalUnitType::DPA:
			[[fallthrough]];
		case H264NalUnitType::DPB:
			[[fallthrough]];
		case H264NalUnitType::DPC:
			[[fallthrough]];
		case H264NalUnitType::IdrSlice:
			[[fallthrough]];
		case H264NalUnitType::Sei:
			[[fallthrough]];
		case H264NalUnitType::Sps:
			[[fallthrough]];
		case H264NalUnitType::Pps:
			[[fallthrough]];
		case H264NalUnitType::Aud:
			[[fallthrough]];
		case H264NalUnitType::EndOfSequence:
			[[fallthrough]];
		case H264NalUnitType::EndOfStream:
			[[fallthrough]];
		case H264NalUnitType::FillerData:
			[[fallthrough]];
		case H264NalUnitType::SpsExt:
			[[fallthrough]];
		case H264NalUnitType::AuxiliarySlice:
			return true;
	}

	logtw("Unsupported NAL unit type %u", nal_unit_type);
	return false;
}

constexpr const char *NalUnitTypeToStr(H264NalUnitType nal_unit_type)
{
	switch (nal_unit_type)
	{
		// Dimiden - Kept this value as it was returning `Unknown`
		// before refactoring due to not being handled in the case statement
		OV_CASE_RETURN(H264NalUnitType::Unspecified, "Unspecified");
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, NonIdrSlice);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, DPA);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, DPB);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, DPC);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, IdrSlice);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, Sei);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, Sps);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, Pps);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, Aud);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, EndOfSequence);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, EndOfStream);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, FillerData);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, SpsExt);
		OV_CASE_RETURN_ENUM_STRING(H264NalUnitType, AuxiliarySlice);
	}

	return "Unknown";
}

bool operator==(uint8_t first, H264NalUnitType second)
{
	return first == static_cast<uint8_t>(second);
}

bool operator!=(uint8_t first, H264NalUnitType second)
{
	return first != static_cast<uint8_t>(second);
}
