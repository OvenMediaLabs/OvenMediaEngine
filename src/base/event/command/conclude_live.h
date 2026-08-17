//==============================================================================
//
//  MediaEvent Payload
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../event_command.h"

class EventCommandConcludeLive : public EventCommand
{
public:
	EventCommandConcludeLive()
	{
	}

	Type GetType() const override
	{
		return Type::ConcludeLive;
	}

	ov::String GetTypeString() const override
	{
		return "ConcludeLive";
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