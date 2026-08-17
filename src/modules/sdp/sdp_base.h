//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "base/ovlibrary/ovlibrary.h"

class SdpBase
{
public:
	virtual bool FromString(const ov::String &desc) = 0;

	bool Update()
	{
		return UpdateData(_sdp_text);
	}

	ov::String ToString() const
	{
		return _sdp_text;
	}

protected:
	virtual bool UpdateData(ov::String &sdp) = 0;

private:
	ov::String _sdp_text;
};
