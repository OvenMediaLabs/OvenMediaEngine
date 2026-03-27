//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace ov::sig
{
	bool Initialize();
	void SetDumpFallbackPath(const char *path);

	// Returns `true` if a termination request was triggered by a signal.
	bool WaitAndStop(int milliseconds);
}  // namespace ov::sig
