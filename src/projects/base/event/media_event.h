//==============================================================================
//
//  MediaEvent
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/mediarouter/media_buffer.h>
#include "event_command.h"

// This event is delivered to each module sorted by occurrence time along with MediaPackets.
class MediaEvent : public MediaPacket
{
public:
	MediaEvent()
		: MediaPacket(0, cmn::MediaType::Data, 0, nullptr, 0LL, 0LL, 0LL, MediaPacketFlag::Unknown, cmn::BitstreamFormat::OVEN_EVENT, cmn::PacketType::EVENT)
	{
	}

	// MediaPacket to MediaEvent
	static std::shared_ptr<MediaEvent> Convert(const std::shared_ptr<MediaPacket> &packet);
	// Build MediaEvent from EventCommand
	static std::shared_ptr<MediaEvent> BuildEvent(const std::shared_ptr<EventCommand> &command);
	static std::shared_ptr<MediaEvent> BuildEvent(EventCommand::Type  command_type, const std::shared_ptr<ov::Data> &command_data);

	EventCommand::Type GetCommandType() const;
	std::shared_ptr<ov::Data> GetCommandData() const;
	std::shared_ptr<MediaEvent> Clone() const;
	ov::String ToString() const;

	bool SetCommand(EventCommand::Type  command_type, const std::shared_ptr<ov::Data> &command_data);

	template<typename T>
	std::shared_ptr<T> GetCommand() const
	{
		if (_command == nullptr)
		{
			return nullptr;
		}

		return std::dynamic_pointer_cast<T>(_command);
	}

private:
	bool ParseCommandData();
	std::shared_ptr<ov::Data> Serialize(); // Serialize 32-bit command type + command data

	std::shared_ptr<EventCommand> _command = nullptr;

	// Cached command type and data
	EventCommand::Type _command_type = EventCommand::Type::NOP; // command type
	std::shared_ptr<ov::Data> _command_data = nullptr; // command data
};