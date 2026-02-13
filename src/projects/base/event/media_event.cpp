//==============================================================================
//
//  MediaEvent
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "media_event.h"
#include "command/update_language.h"
#include "command/conclude_live.h"

std::shared_ptr<MediaEvent> MediaEvent::Convert(const std::shared_ptr<MediaPacket> &packet)
{
	if (packet == nullptr || packet->GetMediaType() != cmn::MediaType::Data || packet->GetBitstreamFormat() != cmn::BitstreamFormat::OVEN_EVENT)
	{
		return nullptr;
	}

	auto data = packet->GetData();
	if (data == nullptr || data->GetLength() < sizeof(uint32_t))
	{
		return nullptr;
	}

	auto raw_data = data->GetDataAs<const uint8_t>();
	if (raw_data == nullptr)
	{
		return nullptr;
	}

	uint32_t command_type_value = 0;
	memcpy(&command_type_value, raw_data, sizeof(uint32_t));
	EventCommand::Type  command_type = static_cast<EventCommand::Type>(command_type_value);

	std::shared_ptr<ov::Data> command_data = nullptr;
	size_t command_data_length = data->GetLength() - sizeof(uint32_t);
	if (command_data_length > 0)
	{
		command_data = std::make_shared<ov::Data>(raw_data + sizeof(uint32_t), command_data_length);
	}

	auto event = std::make_shared<MediaEvent>();
	if (event->SetCommand(command_type, command_data) == false)
	{
		return nullptr;
	}

	// Copy MediaPacket members
	event->SetMsid(packet->GetMsid());
	event->SetTrackId(packet->GetTrackId());
	event->SetPts(packet->GetPts());
	event->SetDts(packet->GetDts());
	event->SetDuration(packet->GetDuration());
	event->SetFlag(packet->GetFlag());
	event->SetBitstreamFormat(packet->GetBitstreamFormat());
	event->SetPacketType(packet->GetPacketType());
	event->SetHighPriority(packet->IsHighPriority());

	auto cloned_data = data->Clone();
	event->SetData(cloned_data);

	return event;
}

std::shared_ptr<MediaEvent> MediaEvent::BuildEvent(const std::shared_ptr<EventCommand> &command)
{
	if (command == nullptr)
	{
		return nullptr;
	}

	auto event = std::make_shared<MediaEvent>();
	event->_command = command;
	event->_command_type = command->GetType();
	event->_command_data = command->ToData();

	auto event_data = event->Serialize();
	event->SetData(event_data);
	
	return event;
}

std::shared_ptr<MediaEvent> MediaEvent::BuildEvent(EventCommand::Type  command_type, const std::shared_ptr<ov::Data> &command_data)
{
	auto event = std::make_shared<MediaEvent>();
	if (event->SetCommand(command_type, command_data) == false)
	{
		return nullptr;
	}

	auto event_data = event->Serialize();
	event->SetData(event_data);

	return event;
}

std::shared_ptr<ov::Data> MediaEvent::Serialize()
{
	// Make MediaPacket Data
	auto command_type_value = static_cast<uint32_t>(_command_type);
	size_t command_data_length = (_command_data != nullptr) ? _command_data->GetLength() : 0;

	auto data = std::make_shared<ov::Data>(sizeof(uint32_t) + command_data_length);
	if (data == nullptr || data->GetLength() != sizeof(uint32_t) + command_data_length)
	{
		return nullptr;
	}

	auto raw_data = data->GetWritableDataAs<uint8_t>();
	if (raw_data == nullptr)
	{
		return nullptr;
	}

	memcpy(raw_data, &command_type_value, sizeof(uint32_t));
	if (command_data_length > 0)
	{
		memcpy(raw_data + sizeof(uint32_t), _command_data->GetData(), command_data_length);
	}

	return data;
}

bool MediaEvent::SetCommand(EventCommand::Type  command_type, const std::shared_ptr<ov::Data> &command_data)
{
	_command_type = command_type;
	_command_data = command_data;
	_command = nullptr;

	if (ParseCommandData() == false)
	{
		return false;
	}

	return true;
}

bool MediaEvent::ParseCommandData()
{
	switch (_command_type)
	{
	case EventCommand::Type::UpdateSubtitleLanguage:
		if (_command_data == nullptr || _command_data->GetLength() == 0)
		{
			return false;
		}
		_command = std::make_shared<EventCommandUpdateLanguage>();
		break;
	case EventCommand::Type::ConcludeLive:
		// No payload
		_command = std::make_shared<EventCommandConcludeLive>();
		break;
	case EventCommand::Type::NOP:
		// No payload
		return true;
	default:
		return false;
	}

	if (_command != nullptr)
	{
		if (_command_data == nullptr || _command_data->GetLength() == 0)
		{
			// No data to parse
			return true;
		}

		return _command->Parse(_command_data);
	}

	return false;
}

EventCommand::Type MediaEvent::GetCommandType() const
{
	return _command_type;
}

std::shared_ptr<ov::Data> MediaEvent::GetCommandData() const
{
	return _command_data;
}

std::shared_ptr<MediaEvent> MediaEvent::Clone() const
{
	auto cloned_event = std::make_shared<MediaEvent>();
	if (cloned_event->SetCommand(_command_type, _command_data) == false)
	{
		return nullptr;
	}

	// Copy MediaPacket members
	cloned_event->SetMsid(GetMsid());
	cloned_event->SetTrackId(GetTrackId());

	cloned_event->SetPts(GetPts());
	cloned_event->SetDts(GetDts());
	cloned_event->SetDuration(GetDuration());
	cloned_event->SetFlag(GetFlag());
	cloned_event->SetBitstreamFormat(GetBitstreamFormat());
	cloned_event->SetPacketType(GetPacketType());
	cloned_event->SetHighPriority(IsHighPriority());

	return cloned_event;
}

ov::String MediaEvent::ToString() const
{
	if (_command != nullptr)
	{
		return ov::String::FormatString("MediaEvent(Type=%s, DataLength=%zu)", _command->GetTypeString().CStr(), (_command_data != nullptr) ? _command_data->GetLength() : 0);
	}
	else
	{
		return ov::String::FormatString("MediaEvent(Type=%d, DataLength=%zu)", static_cast<uint32_t>(_command_type), (_command_data != nullptr) ? _command_data->GetLength() : 0);
	}
}