//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <monitoring/monitoring.h>
#include "base/info/codec.h"
namespace serdes
{
	Json::Value JsonFromCodecModule(const std::shared_ptr<info::CodecModule> &module);
}  // namespace serdes