//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "application.h"
#include "common.h"
namespace serdes
{
	Json::Value JsonFromMetrics(const std::shared_ptr<const mon::CommonMetrics> &metrics)
	{
		if (metrics == nullptr)
		{
			return Json::nullValue;
		}

		Json::Value value;

		SetTimestamp(value, "createdTime", metrics->GetCreatedTime());
		SetTimestamp(value, "lastUpdatedTime", metrics->GetLastUpdatedTime());
		SetInt64(value, "totalBytesIn", metrics->GetTotalBytesIn());
		SetInt64(value, "totalBytesOut", metrics->GetTotalBytesOut());
		SetInt64(value, "avgThroughputIn", metrics->GetAvgThroughputIn());
		SetInt64(value, "avgThroughputOut", metrics->GetAvgThroughputOut());		
		SetInt64(value, "maxThroughputIn", metrics->GetMaxThroughputIn());
		SetInt64(value, "maxThroughputOut", metrics->GetMaxThroughputOut());
		SetInt64(value, "lastThroughputIn", metrics->GetLastThroughputIn());
		SetInt64(value, "lastThroughputOut", metrics->GetLastThroughputOut());
		SetTimestamp(value, "lastRecvTime", metrics->GetLastRecvTime());
		SetTimestamp(value, "lastSentTime", metrics->GetLastSentTime());
		SetInt(value, "totalConnections", metrics->GetTotalConnections());
		SetInt(value, "maxTotalConnections", metrics->GetMaxTotalConnections());
		SetTimestamp(value, "maxTotalConnectionTime", metrics->GetMaxTotalConnectionsTime());

		Json::Value &connections = value["connections"];

		auto target_publishers = {
			PublisherType::Webrtc,
			PublisherType::LLHls,
			PublisherType::Ovt,
			PublisherType::File,
			PublisherType::Push,
			PublisherType::Thumbnail,
			PublisherType::Hls,
			PublisherType::Srt,
		};

		for (auto publisher : target_publishers)
		{
			auto name = StringFromPublisherType(publisher).LowerCaseString();

			SetInt(connections, name, metrics->GetConnections(publisher));
		}

		return value;
	}

	Json::Value JsonFromStreamMetrics(const std::shared_ptr<const mon::StreamMetrics> &metrics)
	{
		Json::Value value = JsonFromMetrics(metrics);

		if (value.isNull())
		{
			return value;
		}

		SetTimeInterval(value, "requestTimeToOrigin", metrics->GetOriginConnectionTimeMSec());
		SetTimeInterval(value, "responseTimeFromOrigin", metrics->GetOriginSubscribeTimeMSec());

		return value;
	}

	Json::Value JsonFromQueueMetrics(const std::shared_ptr<const mon::QueueMetrics> &metrics)
	{
		if (metrics == nullptr)
		{
			return Json::nullValue;
		}

		Json::Value value;

		SetInt64(value, "id", metrics->GetId());
		// A queue can be registered before its URN is set (e.g. default-constructed
		// queue, URN assigned later via SetUrn), so GetUrn() may be null. Use the same
		// "No Urn" sentinel as info::ManagedQueue::ToString(); an empty string would trip
		// the non-optional SetString assert and omit the key.
		auto urn = metrics->GetUrn();
		SetString(value, "urn", (urn != nullptr) ? urn->ToString() : ov::String("No Urn"), Optional::False);
		SetString(value, "type", metrics->GetTypeName(), Optional::False);
		SetInt(value, "size", metrics->GetSize());
		SetInt(value, "peak", metrics->GetPeak());
		SetInt(value, "threshold", metrics->GetThreshold());
		SetInt64(value, "avgWaitingTime", metrics->GetWaitingTime());
		SetInt64(value, "dwellMinUs", metrics->GetDwellMin());
		SetInt64(value, "dwellAvgUs", metrics->GetDwellAvg());
		SetInt64(value, "dwellP50Us", metrics->GetDwellP50());
		SetInt64(value, "dwellP90Us", metrics->GetDwellP90());
		SetInt64(value, "dwellP99Us", metrics->GetDwellP99());
		SetInt64(value, "dwellMaxUs", metrics->GetDwellMax());
		SetInt(value, "inputPerSecond", metrics->GetInputMessagePerSecond());
		SetInt(value, "outputPerSecond", metrics->GetOutputMessagePerSecond());
		SetInt(value, "drop", metrics->GetDropCount());

		return value;
	}
}  // namespace serdes