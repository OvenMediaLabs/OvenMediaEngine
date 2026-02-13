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
	return _engine;
}
void SubtitleTrack::SetEngine(const ov::String &engine)
{
	_engine = engine;
}

void SubtitleTrack::SetModel(const ov::String &model)
{
	_model = model;
}
ov::String SubtitleTrack::GetModel() const
{
	return _model;
}

void SubtitleTrack::SetSourceLanguage(const ov::String &language)
{
	_source_language = language;
}
ov::String SubtitleTrack::GetSourceLanguage() const
{
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
	_output_track_label = label;
}
ov::String SubtitleTrack::GetOutputTrackLabel() const
{
	return _output_track_label;
}