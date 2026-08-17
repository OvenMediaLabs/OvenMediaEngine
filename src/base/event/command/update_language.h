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

class EventCommandUpdateLanguage : public EventCommand
{
public:
	static std::shared_ptr<EventCommandUpdateLanguage> Create(const ov::String &track_label, const ov::String &language)
	{
		return std::make_shared<EventCommandUpdateLanguage>(track_label, language);
	}

	EventCommandUpdateLanguage()
	{
	}

	EventCommandUpdateLanguage(const ov::String &track_label, const ov::String &language)
		: _track_label(track_label), _language(language)
	{
	}

	ov::String GetTrackLabel() const
	{
		return _track_label;
	}

	ov::String GetLanguage() const
	{
		return _language;
	}

	Type GetType() const override
	{
		return Type::UpdateSubtitleLanguage;
	}

	ov::String GetTypeString() const override
	{
		return "UpdateSubtitleLanguage";
	}

	bool Parse(const std::shared_ptr<ov::Data> &data) override
	{
		if (data == nullptr || data->GetLength() == 0)
		{
			return false;
		}

		auto object = ov::Json::Parse(data->ToString());
		if (object.IsNull())
		{
			return false;
		}

		if (object.IsMember("trackLabel") == false || object.IsMember("language") == false ||
			object.GetJsonValue("trackLabel").isString() == false || object.GetJsonValue("language").isString() == false)
		{
			return false;
		}

		_track_label = object.GetStringValue("trackLabel");
		_language = object.GetStringValue("language");

		return true;
	}

	std::shared_ptr<ov::Data> ToData() const override
	{
		ov::JsonObject object;
		object.GetJsonValue()["trackLabel"] = _track_label.CStr();
		object.GetJsonValue()["language"] = _language.CStr();

		auto str = ov::Json::Stringify(object);
		auto data = std::make_shared<ov::Data>(str.CStr(), str.GetLength());

		return data;
	} 

private:
	ov::String _track_label;
	ov::String _language;
};