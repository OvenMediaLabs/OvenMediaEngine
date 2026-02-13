//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/rtmp/amf0/amf_document.h>
#include <pugixml-1.9/src/pugixml.hpp>

/*
Example JSON format
-------------------
// Data is consist of object array
{
	"eventFormat": "amf",
	"events": [
		{
			"amfType": "onUserData | onUserDataEvent",
			"data": {
				"key1": "value",    // String Type
				"key3": 354.1,      // Number Type [Double]
				"key4": true        // Boolean Type [true | false]
			}
		}
	]
}

OR

// Data is single value, not object array
{
	"eventFormat": "amf",
	"events": [
		{
			"amfType": "onUserData | onUserDataEvent",
			"data": "Hello. OvenMediaEngine."   // String Type | Number Type [Double] | Boolean Type [true | false]
		}
	]
}
*/

/*
Example XML format
------------------
<Event>
	<Enable>true</Enable>
	<SourceStreamName>stream</SourceStreamName>
	<Interval>5000</Interval>
	<EventFormat>amf</EventFormat>
	<Values>
		<AmfType>onUserDataEvent</AmfType>
		<Data>
			<Key0>value</Key0>
			<Key1 type="string">value</Key1>
			<Key2 type="boolean">false</Key2>
			<Key3 type="double">351.4</Key3>
		</Data>
	</Values>
</Event>

or

	<Event>
	<Enable>true</Enable>
	<SourceStreamName>stream</SourceStreamName>
	<Interval>5000</Interval>
	<EventFormat>amf</EventFormat>
	<Values>
		<AmfType>onUserDataEvent</AmfType>
		<Data><![CDATA[Hello. OvenMediaEngine.]]></Data>
	</Values>
</Event>	
*/

class AmfUserDataEvent
{
public:
	static bool IsMatch(const ov::String &amf_type);
	static std::shared_ptr<AmfUserDataEvent> Create();
	static std::shared_ptr<AmfUserDataEvent> Parse(const std::shared_ptr<ov::Data> &data);

	static bool IsValid(const Json::Value &json);
	static std::shared_ptr<AmfUserDataEvent> Parse(const Json::Value &json);

	static bool IsValid(const pugi::xml_node &xml);
	static std::shared_ptr<AmfUserDataEvent> Parse(const pugi::xml_node &xml);

	AmfUserDataEvent();
	~AmfUserDataEvent() = default;

	std::shared_ptr<ov::Data> Serialize() const;

	// Set Command Type. ex) onUserData, OnUserDataEvent
	void SetType(const ov::String &type) {
		_type = type;
	}
	
	template <typename T>
    bool Append(const char* name, T&& value)
    {
        if(_arrays == nullptr)
		{
			_arrays = std::make_shared<AmfEcmaArray>();
		}
        return _arrays->Append(name, AmfProperty(std::forward<T>(value)));
    }

	template <typename T>
	bool SetData(T&& value)
	{
		_single_data = std::make_shared<AmfProperty>(std::forward<T>(value));
		return true;
	}

private:
	ov::String _type;
	std::shared_ptr<AmfEcmaArray> _arrays	  = nullptr;
	std::shared_ptr<AmfProperty> _single_data = nullptr;
};