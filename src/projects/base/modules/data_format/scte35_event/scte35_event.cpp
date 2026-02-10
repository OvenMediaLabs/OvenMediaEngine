//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "scte35_event.h"

#include <modules/containers/mpegts/mpegts_section.h>
#include <modules/containers/mpegts/scte35/mpegts_splice_insert.h>

std::shared_ptr<Scte35Event> Scte35Event::Create(mpegts::SpliceCommandType splice_command_type, uint32_t id, bool out_of_network, int64_t timestamp_msec, int64_t duration_msec, bool auto_return)
{
	return std::make_shared<Scte35Event>(splice_command_type, id, out_of_network, timestamp_msec, duration_msec, auto_return);
}

std::shared_ptr<Scte35Event> Scte35Event::Create(const std::shared_ptr<const mpegts::SpliceInfo> &splice_info)
{
	if (splice_info == nullptr)
	{
		return nullptr;
	}

	if (splice_info->GetSpliceCommandType() != mpegts::SpliceCommandType::SPLICE_INSERT)
	{
		return nullptr;
	}

	// Cast to SpliceInsert
	auto splice_insert = std::dynamic_pointer_cast<const mpegts::SpliceInsert>(splice_info);
	if (splice_insert == nullptr)
	{
		return nullptr;
	}

	return Create(splice_insert->GetSpliceCommandType(),
				  splice_insert->GetSpliceEventId(),
				  splice_insert->GetOutofNetworkIndicator() != 0,
				  splice_insert->GetPTSTime() / 90,
				  splice_insert->GetDuration() / 90,
				  splice_insert->GetAutoReturn());
}

Scte35Event::Scte35Event(mpegts::SpliceCommandType splice_command_type, uint32_t id, bool out_of_network, int64_t timestamp_msec, int64_t duration_msec, bool auto_return)
{
	_splice_command_type = splice_command_type;
	_id = id;
	_out_of_network = out_of_network;
	_timestamp_msec = timestamp_msec;
	_duration_msec = duration_msec;
	_auto_return = auto_return;
}

std::shared_ptr<Scte35Event> Scte35Event::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if (data == nullptr)
	{
		return nullptr;
	}

	ov::ByteStream stream(data);

	if (data->GetLength() < 23)
	{
		return nullptr;
	}

	uint8_t splice_command_type_value = stream.Read8();
	// Now we only support SPLICE_INSERT
	if (splice_command_type_value != static_cast<uint8_t>(mpegts::SpliceCommandType::SPLICE_INSERT))
	{
		return nullptr;
	}

	mpegts::SpliceCommandType splice_command_type = static_cast<mpegts::SpliceCommandType>(splice_command_type_value);
	uint32_t id = stream.ReadBE32();
	bool out_of_network = stream.Read8() == 1;
	int64_t timestamp_msec = stream.ReadBE64();
	int64_t duration_msec = stream.ReadBE64();
	bool auto_return = stream.Read8() == 1;

	return Create(splice_command_type, id, out_of_network, timestamp_msec, duration_msec, auto_return);
}

std::shared_ptr<ov::Data> Scte35Event::Serialize() const
{
	ov::ByteStream stream;
	stream.Write8(static_cast<uint8_t>(_splice_command_type));
	stream.WriteBE32(_id);
	stream.Write8(_out_of_network ? 1 : 0);
	stream.WriteBE64(_timestamp_msec);
	stream.WriteBE64(_duration_msec);
	stream.Write8(_auto_return ? 1 : 0);

	return stream.GetDataPointer();
}

// Getter
mpegts::SpliceCommandType Scte35Event::GetSpliceCommandType() const
{
	return _splice_command_type;
}

uint32_t Scte35Event::GetID() const
{
	return _id;
}

bool Scte35Event::IsOutOfNetwork() const
{
	return _out_of_network;
}

int64_t Scte35Event::GetTimestampMsec() const
{
	return _timestamp_msec;
}

int64_t Scte35Event::GetDurationMsec() const
{
	return _duration_msec;
}

bool Scte35Event::IsAutoReturn() const
{
	return _auto_return;
}

std::shared_ptr<ov::Data> Scte35Event::MakeScteData() const
{
	if (_splice_command_type != mpegts::SpliceCommandType::SPLICE_INSERT)
	{
		return nullptr;
	}

	auto splice_insert = std::make_shared<mpegts::SpliceInsert>(_id);
	splice_insert->SetOutofNetworkIndicator(_out_of_network);
	splice_insert->SetPTS(_timestamp_msec * 90);
	splice_insert->SetDuration(_duration_msec * 90);
	splice_insert->SetAutoReturn(_auto_return);

	auto splice_info_section = mpegts::Section::Build(0x1FFA, splice_insert);
	if (splice_info_section == nullptr)
	{
		return nullptr;
	}

	return splice_info_section->GetData().Clone();
}

ov::String Scte35Event::ToString() const
{
	ov::String str = ov::String::FormatString(
		"SCTE-35 Event: SpliceCommandType=%d, ID=%u, OutOfNetwork=%s, Timestamp=%" PRId64 " ms, Duration=%" PRId64 " ms, AutoReturn=%s",
		static_cast<int>(_splice_command_type),
		_id,
		_out_of_network ? "true" : "false",
		_timestamp_msec,
		_duration_msec,
		_auto_return ? "true" : "false");

	return str;
}