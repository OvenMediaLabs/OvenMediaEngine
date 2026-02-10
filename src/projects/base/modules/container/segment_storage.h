//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/modules/marker/marker_box.h>

namespace base
{
	namespace modules
	{
		class PartialSegment
		{
		public:
			virtual int64_t GetNumber() const = 0;
			virtual int64_t GetStartTimestamp() const = 0;
			virtual double GetDurationMs() const = 0;
			virtual bool IsIndependent() const = 0;
			virtual uint64_t GetDataLength() const = 0;
			virtual const std::shared_ptr<ov::Data> GetData() const = 0;

			virtual ov::String GetUrl() const { return ""; }
			virtual void SetUrl(const ov::String &url) {}

			virtual double GetTimebaseSeconds() const = 0;
		};

		class Segment : public PartialSegment
		{
		public:
			// All partial segments are included
			virtual bool IsCompleted() const = 0;
			virtual bool HasMarker() const = 0;
			virtual const std::vector<std::shared_ptr<Marker>> &GetMarkers() const = 0;
			
			// Segment must be independent
			bool IsIndependent() const override { return true; }
		};

		class SegmentStorage
		{
		public:
			SegmentStorage() = default;
			virtual ~SegmentStorage() = default;

			virtual std::shared_ptr<ov::Data> GetInitializationSection() const = 0;
			virtual std::shared_ptr<Segment> GetSegment(int64_t segment_number) const = 0;
			virtual std::shared_ptr<Segment> GetLastSegment() const = 0;
			virtual std::shared_ptr<PartialSegment> GetPartialSegment(int64_t segment_number, int64_t partial_number) const = 0;

			virtual uint64_t GetSegmentCount() const = 0;

			virtual int64_t GetLastSegmentNumber() const = 0;
			virtual std::tuple<int64_t, int64_t> GetLastPartialSegmentNumber() const = 0;

			virtual uint64_t GetMaxPartialDurationMs() const = 0;
			virtual uint64_t GetMinPartialDurationMs() const = 0;

			virtual ov::String GetContainerExtension() const = 0;
		};
	}
}