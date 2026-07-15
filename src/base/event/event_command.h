//==============================================================================
//
//  MediaEvent Payload
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

class EventCommand
{
public:
	enum class Type : uint32_t
	{
		NOP = 0, // No Operation
		ConcludeLive, // Conclude Live, transits the stream to VOD if possible
		UpdateSubtitleLanguage, // Detected Subtitle Language
		// Data format (json)
		// { "trackLabel": "Origin", "language": "en" }
	};

	virtual ~EventCommand() = default;

	virtual Type GetType() const = 0;
	virtual ov::String GetTypeString() const = 0;
	virtual std::shared_ptr<ov::Data> ToData() const = 0;

	// Header type(4 bytes) + Payload
	virtual bool Parse(const std::shared_ptr<ov::Data> &data) = 0;
};
