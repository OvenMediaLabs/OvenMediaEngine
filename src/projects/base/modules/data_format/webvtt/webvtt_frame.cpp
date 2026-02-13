//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "webvtt_frame.h"

std::shared_ptr<WebVTTFrame> WebVTTFrame::Create(const ov::String &label, int64_t start_time, int64_t end_time, const ov::String &settings, const ov::String &text)
{
	return std::make_shared<WebVTTFrame>(label, start_time, end_time, settings, text);
}
WebVTTFrame::WebVTTFrame(const ov::String &label, int64_t start_time, int64_t end_time, const ov::String &settings, const ov::String &text)
	: _label(label), _start_time_ms(start_time), _end_time_ms(end_time), _settings(settings), _text(text)
{
}
std::shared_ptr<WebVTTFrame> WebVTTFrame::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if (data == nullptr || data->GetLength() < 1)
	{
		return nullptr;
	}

	ov::ByteStream stream(data);

	if (stream.Remained() < 24) // 24 = 4 (label_length) + 8 (start_time) + 8 (end_time) + 4 (settings_length) + 4 (text_length)
	{
		return nullptr;
	}

	// label
	auto label_length = stream.ReadBE32();
	if (label_length < 0 || label_length > stream.Remained())
	{
		return nullptr;
	}
	ov::String label = stream.ReadString(label_length);

	// start_time and end_time
	if (stream.Remained() < 16)
	{
		return nullptr;
	}
	int64_t start_time = stream.ReadBE64();
	int64_t end_time = stream.ReadBE64();

	// settings
	auto settings_length = stream.ReadBE32();
	ov::String settings;
	if (settings_length < 0 || settings_length > stream.Remained()) 
	{
		return nullptr;
	}
	
	if (settings_length > 0)
	{
		settings = stream.ReadString(settings_length);
	}

	// text
	if (stream.Remained() < 4)
	{
		return nullptr;
	}
	auto text_length = stream.ReadBE32();
	ov::String text;
	if (text_length < 0 || text_length > stream.Remained())
	{
		return nullptr;
	}
	
	if (text_length > 0)
	{
		text = stream.ReadString(text_length);
	}

	return Create(label, start_time, end_time, settings, text);
}

std::shared_ptr<ov::Data> WebVTTFrame::Serialize() const
{
	ov::ByteStream stream(10240);

	stream.WriteBE32(_label.GetLength());
	stream.WriteText(_label);
	stream.WriteBE64(_start_time_ms);
	stream.WriteBE64(_end_time_ms);
	stream.WriteBE32(_settings.GetLength());
	stream.WriteText(_settings);
	stream.WriteBE32(_text.GetLength());
	stream.WriteText(_text);

	return stream.GetDataPointer();
}