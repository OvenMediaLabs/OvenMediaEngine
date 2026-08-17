#include "rtsp_header_field_parser.h"

std::shared_ptr<RtspHeaderField> RtspHeaderFieldParser::Parse(const ov::String &message)
{
	/*
	// Header Fileds

		message-header     =      field-name ":" [ field-value ] CRLF
		field-name         =      token
		field-value        =      *( field-content | LWS )
		field-content      =      <the OCTETs making up the field-value and
									consisting of either *TEXT or
									combinations of token, tspecials, and
									quoted-string>
	*/

	// Parsing
	auto index = message.IndexOf(':');
	if(index == -1)
	{
		return nullptr;
	}

	std::shared_ptr<RtspHeaderField> field;
	auto field_name = message.Substring(0, index).Trim();
	if(field_name.UpperCaseString() == RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::Session).UpperCaseString())
	{
		field = std::make_shared<RtspHeaderSessionField>();
	}
	else if(field_name.UpperCaseString() == RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::Transport).UpperCaseString())
	{
		field = std::make_shared<RtspHeaderTransportField>();
	}
	else if(field_name.UpperCaseString() == RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::WWWAuthenticate).UpperCaseString())
	{
		field = std::make_shared<RtspHeaderWWWAuthenticateField>();
	}
	else
	{
		field = std::make_shared<RtspHeaderField>();
	}

	if(field->Parse(message) == false)
	{
		return nullptr;
	}

	return field;
}