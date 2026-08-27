//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/ovlibrary/ovlibrary.h>

// Turns pic_struct_present_flag on in an SPS. Without it a decoder reads neither pic_struct nor
// the clock timestamps of a pic_timing SEI (ITU-T H.264 D.2.2), and encoders commonly leave it at
// 0. It is coded unconditionally inside vui_parameters(), so the RBSP keeps its length.
class H264SpsRewriter
{
public:
	// In: the SPS NAL as it appears in the bitstream. Out: the same NAL unescaped - header still
	// there, so not a bare RBSP - because NalUnitInsertor escapes whatever NAL it is given.
	//
	// nullptr when the flag is already 1, the SPS does not parse, or there is no VUI to carry it.
	static std::shared_ptr<ov::Data> EnablePicStructPresent(const std::shared_ptr<const ov::Data> &sps_nal);
};
