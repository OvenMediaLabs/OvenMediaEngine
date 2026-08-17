//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"

namespace info
{
	class EventForward
	{
	public:
		explicit EventForward();

		const std::vector<cmn::BitstreamFormat> &GetDropEvents() const;

		void AddDropEventString(ov::String event_string);
		const std::vector<ov::String> &GetDropEventsString() const;
		bool Validate();

		ov::String ToString() const;

	protected:
		const std::vector<cmn::BitstreamFormat> _available_events =
			{
				cmn::BitstreamFormat::ID3v2,
				cmn::BitstreamFormat::OVEN_EVENT,
				cmn::BitstreamFormat::CUE,
				cmn::BitstreamFormat::SEI,
				cmn::BitstreamFormat::SCTE35,
				cmn::BitstreamFormat::WebVTT,
				cmn::BitstreamFormat::AMF};

	private:
		std::vector<cmn::BitstreamFormat> _drop_events;
		std::vector<ov::String> _drop_events_string;
	};

}  // namespace info