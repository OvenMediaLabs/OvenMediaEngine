//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

/*
[Label Length] - 4 bytes
[Label] - variable length, UTF-8 encoded string
[Start Time] - 8 bytes, in milliseconds
[End Time] - 8 bytes, in milliseconds
[Settings Length] - 4 bytes
[Settings] - variable length, UTF-8 encoded string
[Text Length] - 4 bytes
[Text] - variable length, UTF-8 encoded string
*/

// Timescale : 1000 (milliseconds)

class WebVTTFrame
{
public:
	static std::shared_ptr<WebVTTFrame> Create(const ov::String &label, int64_t start_time_ms, int64_t end_time, const ov::String &settings, const ov::String &text);
	static std::shared_ptr<WebVTTFrame> Parse(const std::shared_ptr<const ov::Data> &data);

	WebVTTFrame(const ov::String &label, int64_t start_time, int64_t end_time, const ov::String &settings, const ov::String &text);
	~WebVTTFrame() = default;

	std::shared_ptr<ov::Data> Serialize() const;

	// Getters
	const ov::String &GetLabel() const { return _label; }
	int64_t GetStartTimeMs() const { return _start_time_ms; }
	int64_t GetEndTimeMs() const { return _end_time_ms; }
	const ov::String &GetSettings() const { return _settings; }
	const ov::String &GetText() const { return _text; }

	const ov::String ToVttFormatText(int64_t offset) const
	{
		int64_t start_time_sec = (_start_time_ms - offset) / 1000;
		int64_t start_time_sec_remainder = (_start_time_ms - offset) % 1000;
		int64_t end_time_sec = (_end_time_ms - offset) / 1000;
		int64_t end_time_sec_remainder = (_end_time_ms - offset) % 1000;

		ov::String vtt_text;

		// 00:00:00.000 --> 00:00:05.000
		vtt_text += ov::String::FormatString("%02" PRId64 ":%02" PRId64 ":%02" PRId64 ".%03" PRId64 " --> %02" PRId64 ":%02" PRId64 ":%02" PRId64 ".%03" PRId64 " %s\n", 
			(start_time_sec / 3600), (start_time_sec % 3600) / 60, (start_time_sec % 60), start_time_sec_remainder,
			(end_time_sec / 3600), (end_time_sec % 3600) / 60, (end_time_sec % 60), end_time_sec_remainder,
			_settings.IsEmpty() ? "" : _settings.CStr());
		vtt_text += _text;
		vtt_text += "\n";

		return vtt_text;
	}

	void MarkAsUsed() { _used = true; }
	bool IsUsed() const { return _used; }

private:
	ov::String _label;
	int64_t _start_time_ms = 0;
	int64_t _end_time_ms = 0;
	ov::String _settings; 
	ov::String _text;

	bool _used = false; // for debugging
};