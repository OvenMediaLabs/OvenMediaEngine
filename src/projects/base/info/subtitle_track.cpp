//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "subtitle_track.h"

void SubtitleTrack::SetAutoSelect(bool auto_select)
{
	_auto_select = auto_select;
}
bool SubtitleTrack::IsAutoSelect() const
{
	return _auto_select;
}

void SubtitleTrack::SetDefault(bool def)
{
	_default = def;
}
bool SubtitleTrack::IsDefault() const
{
	return _default;
}

void SubtitleTrack::SetForced(bool forced)
{
	_forced = forced;
}
bool SubtitleTrack::IsForced() const
{
	return _forced;
}

ov::String SubtitleTrack::GetEngine() const
{
	std::shared_lock lock(_subtitle_mutex);
	return _engine;
}
void SubtitleTrack::SetEngine(const ov::String &engine)
{
	std::scoped_lock lock(_subtitle_mutex);
	_engine = engine;
}

void SubtitleTrack::SetModel(const ov::String &model)
{
	std::scoped_lock lock(_subtitle_mutex);
	_model = model;
}
ov::String SubtitleTrack::GetModel() const
{
	std::shared_lock lock(_subtitle_mutex);
	return _model;
}

void SubtitleTrack::SetSourceLanguage(const ov::String &language)
{
	std::scoped_lock lock(_subtitle_mutex);
	_source_language = language;
}
ov::String SubtitleTrack::GetSourceLanguage() const
{
	std::shared_lock lock(_subtitle_mutex);
	return _source_language;
}

void SubtitleTrack::SetTranslation(bool translation)
{
	_translation = translation;
}
bool SubtitleTrack::ShouldTranslate() const
{
	return _translation;
}

void SubtitleTrack::SetOutputLabel(const ov::String &label)
{
	std::scoped_lock lock(_subtitle_mutex);
	_output_track_label = label;
}
ov::String SubtitleTrack::GetOutputTrackLabel() const
{
	std::shared_lock lock(_subtitle_mutex);
	return _output_track_label;
}

void SubtitleTrack::SetStepMs(int32_t step_ms) { _step_ms = step_ms; }
int32_t SubtitleTrack::GetStepMs() const { return _step_ms; }

void SubtitleTrack::SetLengthMs(int32_t length_ms) { _length_ms = length_ms; }
int32_t SubtitleTrack::GetLengthMs() const { return _length_ms; }

void SubtitleTrack::SetKeepMs(int32_t keep_ms) { _keep_ms = keep_ms; }
int32_t SubtitleTrack::GetKeepMs() const { return _keep_ms; }