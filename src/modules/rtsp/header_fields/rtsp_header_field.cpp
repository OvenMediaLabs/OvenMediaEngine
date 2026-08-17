#include "rtsp_header_field.h"

RtspHeaderField::RtspHeaderField()
{

}

RtspHeaderField::RtspHeaderField(RtspHeaderFieldType type)
{
	_type = type;
}

RtspHeaderField::RtspHeaderField(RtspHeaderFieldType type, ov::String value)
{
	_type = type;
	_name = RtspHeaderField::FieldTypeToString(type);
	_value = value;
}

RtspHeaderField::RtspHeaderField(RtspHeaderFieldType type, int32_t value)
{
	_type = type;
	_name = RtspHeaderField::FieldTypeToString(type);
	_value = ov::Converter::ToString(value);
}

RtspHeaderField::RtspHeaderField(ov::String name, int32_t value)
{
	SetContent(name, value);
}

RtspHeaderField::RtspHeaderField(ov::String name, ov::String value)
{
	SetContent(name, value);
}

ov::String RtspHeaderField::FieldTypeToString(RtspHeaderFieldType type)
{
	switch(type)
	{
		case RtspHeaderFieldType::ContentType:
			return "Content-Type";
		case RtspHeaderFieldType::ContentBase:
			return "Content-Base";
		case RtspHeaderFieldType::Date:
			return "Date";
		case RtspHeaderFieldType::CSeq:
			return "CSeq";
		case RtspHeaderFieldType::UserAgent:
			return "User-Agent";
		case RtspHeaderFieldType::Server:
			return "Server";
		case RtspHeaderFieldType::CacheControl:
			return "Cache-Control";
		case RtspHeaderFieldType::Public:
			return "Public";
		case RtspHeaderFieldType::Unsupported:
			return "Unsupported";
		case RtspHeaderFieldType::Accept:
			return "Accept";
		case RtspHeaderFieldType::ContentLength:
			return "Content-Length";
		case RtspHeaderFieldType::Session:
			return "Session";
		case RtspHeaderFieldType::Transport:
			return "Transport";
		case RtspHeaderFieldType::Expires:
			return "Expires";
		case RtspHeaderFieldType::Range:
			return "Range";
		case RtspHeaderFieldType::WWWAuthenticate:
			return "WWW-Authenticate";
		case RtspHeaderFieldType::Authorization:
			return "Authorization";
		case RtspHeaderFieldType::Unknown:
		default:
			return "Unknown";	
	}
}

bool RtspHeaderField::Parse(const ov::String &message)
{
	std::shared_ptr<RtspHeaderField> field = nullptr;
	
	logt("DEBUG", "RtspHeaderField::Parse(%s)", message.CStr());

	// Parsing
	auto index = message.IndexOf(':');
	if(index == -1)
	{
		return false;
	}

	auto name = message.Substring(0, index).Trim();
	auto value = message.Substring(index + 1).Trim();
	
	return SetContent(name, value);
}

bool RtspHeaderField::SetContent(RtspHeaderFieldType type, ov::String value)
{
	return SetContent(RtspHeaderField::FieldTypeToString(type), value);
}

bool RtspHeaderField::SetContent(ov::String name, ov::String value)
{
	_name = name;
	_value = value;

	return true;
}

bool RtspHeaderField::SetValue(ov::String value)
{
	_value = value;
	return true;
}

bool RtspHeaderField::SetContent(ov::String name, int32_t value)
{
	return SetContent(name, ov::String::FormatString("%d", value));
}

ov::String RtspHeaderField::Serialize() const
{
	return ov::String::FormatString("%s: %s", _name.CStr(), _value.CStr()); 
}

ov::String RtspHeaderField::GetName()
{
	return _name;
}

ov::String RtspHeaderField::GetValue()
{
	return _value;
}

int32_t RtspHeaderField::GetValueAsInteger()
{
	return std::atoi(_value.CStr());
}