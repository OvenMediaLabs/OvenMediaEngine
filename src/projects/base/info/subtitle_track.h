//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"

class SubtitleTrack
{
public:
	void SetAutoSelect(bool auto_select);
	bool IsAutoSelect() const;

	void SetDefault(bool def);
	bool IsDefault() const;

	void SetForced(bool forced);
	bool IsForced() const;

	ov::String GetEngine() const;
	void SetEngine(const ov::String &engine);

	void SetModel(const ov::String &model);
	ov::String GetModel() const;

	void SetSourceLanguage(const ov::String &language);
	ov::String GetSourceLanguage() const;

	void SetTranslation(bool translation);
	bool ShouldTranslate() const;

	void SetOutputLabel(const ov::String &label);
	ov::String GetOutputTrackLabel() const;

protected:
	mutable std::shared_mutex _subtitle_mutex;

	// For subtitle 
	std::atomic<bool> _auto_select = false;
	std::atomic<bool> _default = false;
	std::atomic<bool> _forced = false;

	// AI Modules
	// e.g. Speech to Text
	ov::String _engine = "whisper"; // Whisper
	ov::String _model = "small"; // tiny, base, small, medium, large
	ov::String _source_language = "auto"; // input language
	std::atomic<bool> _translation = false; // whisper only supports english translation
	ov::String _output_track_label = ""; // input audio track label for speech to text
};