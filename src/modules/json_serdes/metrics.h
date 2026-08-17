//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <monitoring/monitoring.h>

namespace serdes
{
	Json::Value JsonFromMetrics(const std::shared_ptr<const mon::CommonMetrics> &metrics);
	Json::Value JsonFromStreamMetrics(const std::shared_ptr<const mon::StreamMetrics> &metrics);
	Json::Value JsonFromQueueMetrics(const std::shared_ptr<const mon::QueueMetrics> &metrics);
}  // namespace serdes