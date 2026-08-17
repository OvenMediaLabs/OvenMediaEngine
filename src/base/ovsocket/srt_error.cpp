//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "srt_error.h"

#include <srt/srt.h>

namespace ov
{
	std::shared_ptr<SrtError> SrtError::CreateErrorFromSrt()
	{
		auto srt_error = ::srt_getlasterror(nullptr);
		auto srt_error_string = String::FormatString(
			"%s (0x%x)",
			::srt_getlasterror_str(),
			srt_error);

		return std::make_shared<SrtError>(srt_error, srt_error_string);
	}
}  // namespace ov
