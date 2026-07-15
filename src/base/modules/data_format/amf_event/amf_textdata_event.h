//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/rtmp/amf0/amf_document.h>
#include <pugixml-1.9/src/pugixml.hpp>
/*
Example JSON format
-------------------
{
	"eventFormat": "amf",
	"events": [
		{
			"amfType": "onTextData",
			"data": {
				"key1": "value",    // String Type
				"key3": 354.1,       // Number Type [Double]
				"key4": true        // Boolean Type [true | false]
			}
		}
	]
}
*/
/*
Example XML format
-------------------
<Event>
	<Enable>true</Enable>							// Optional (default: false)
	<SourceStreamName>*</SourceStreamName>			// Must
	<Interval>2000</Interval>						// Must
	<EventFormat>amf</EventFormat>					// Must
	<EventType>video</EventType>					// Optional (default: video)
	<Values>										// Must
		<AmfType>onTextData</AmfType>				// Optional (default: onTextData)	
		<Data>                                  	// Must
			<Key0>value</Key0>,						// String Type, Default is String Type
			<Key1 type="string">value</Key1>,		// String Type
			<Key2 type="double">354.1</Key2>,		// Number Type [Double]
			<Key3 type="boolean">true</Key3>		// Boolean Type [true | false]
		</Data>
	</Values>
</Event>
*/

class AmfTextDataEvent
{
public:
	static bool IsMatch(const ov::String &amf_type);
	static std::shared_ptr<AmfTextDataEvent> Create();
	static std::shared_ptr<AmfTextDataEvent> Parse(const std::shared_ptr<ov::Data> &data);

	static bool IsValid(const Json::Value &json);
	static std::shared_ptr<AmfTextDataEvent> Parse(const Json::Value &json);

	static bool IsValid(const pugi::xml_node &xml);
	static std::shared_ptr<AmfTextDataEvent> Parse(const pugi::xml_node &xml);

	AmfTextDataEvent();
	~AmfTextDataEvent() = default;

	std::shared_ptr<ov::Data> Serialize() const;

	template <typename T>
    bool Append(const char* name, T&& value)
    {
        if(_arrays == nullptr)
		{
			_arrays = std::make_shared<AmfEcmaArray>();
		}
        return _arrays->Append(name, AmfProperty(std::forward<T>(value)));
    }

private:
	std::shared_ptr<AmfEcmaArray> _arrays;;
};