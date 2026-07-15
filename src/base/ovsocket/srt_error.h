//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "socket_error.h"

namespace ov
{
	class SrtError : public Error
	{
	public:
		SrtError(int code, const String &message)
			: Error("SRT", code, message)
		{
		}

		static std::shared_ptr<SrtError> CreateErrorFromSrt();
	};
}  // namespace ov
