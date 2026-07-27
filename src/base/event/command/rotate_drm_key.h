//==============================================================================
//
//  MediaEvent Payload
//
//  Created by Getroot
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include "../event_command.h"

class EventCommandRotateDrmKey : public EventCommand
{
public:
	EventCommandRotateDrmKey()
	{
	}

	Type GetType() const override
	{
		return Type::RotateDrmKey;
	}

	ov::String GetTypeString() const override
	{
		return "RotateDrmKey";
	}

	bool Parse(const std::shared_ptr<ov::Data> &data) override
	{
		return true;
	}

	std::shared_ptr<ov::Data> ToData() const override
	{
		return std::make_shared<ov::Data>();
	}
};
