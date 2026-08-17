#include "h265_types.h"

bool operator==(uint8_t first, H265NALUnitType second)
{
	return first == static_cast<uint8_t>(second);
}

bool operator!=(uint8_t first, H265NALUnitType second)
{
	return first != static_cast<uint8_t>(second);
}

bool operator<<(H265NALUnitType first, uint8_t second)
{
	return ov::ToUnderlyingType(first) == second;
}

bool IsH265KeyFrame(H265NALUnitType nal_unit_type)
{
	switch(nal_unit_type)
	{
		OV_CASE_RETURN(H265NALUnitType::BLA_W_LP, true);
		OV_CASE_RETURN(H265NALUnitType::BLA_W_RADL, true);
		OV_CASE_RETURN(H265NALUnitType::BLA_N_LP, true);
		OV_CASE_RETURN(H265NALUnitType::IDR_W_RADL, true);
		OV_CASE_RETURN(H265NALUnitType::IDR_N_LP, true);
		OV_CASE_RETURN(H265NALUnitType::CRA_NUT, true);
		OV_CASE_RETURN(H265NALUnitType::RSV_IRAP_VCL22, true);
		OV_CASE_RETURN(H265NALUnitType::RSV_IRAP_VCL23, true);

		default:
			return false;
	}
}