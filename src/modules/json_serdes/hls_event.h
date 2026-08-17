//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <monitoring/monitoring.h>
#include "base/info/dump.h"

namespace serdes
{
	std::shared_ptr<info::Dump>  DumpInfoFromJson(const Json::Value &json_body);
	Json::Value JsonFromDumpInfo(const std::shared_ptr<const info::Dump> &dump);
}  // namespace serdes