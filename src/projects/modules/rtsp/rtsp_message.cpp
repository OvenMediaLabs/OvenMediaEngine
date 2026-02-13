#include "rtsp_message.h"
#include "header_fields/rtsp_header_field_parser.h"

#define OV_LOG_TAG "RtspMessage"

ov::String RtspMessage::RtspMethodToString(RtspMethod method) const
{
	switch(method)
	{
		case RtspMethod::DESCRIBE:
			return "DESCRIBE";
		case RtspMethod::ANNOUNCE:
			return "ANNOUNCE";
		case RtspMethod::GET_PARAMETER:
			return "GET_PARAMETER";
		case RtspMethod::OPTIONS:
			return "OPTIONS";
		case RtspMethod::PAUSE:
			return "PAUSE";
		case RtspMethod::PLAY:
			return "PLAY";
		case RtspMethod::RECORD:
			return "RECORD";
		case RtspMethod::REDIRECT:
			return "REDIRECT";
		case RtspMethod::SETUP:
			return "SETUP";
		case RtspMethod::SET_PARAMETER:
			return "SET_PARAMETER";
		case RtspMethod::TEARDOWN:
			return "TEARDOWN";
		case RtspMethod::UNKNOWN:
		default:
			return "UNKNOWN";
	}
}

std::tuple<std::shared_ptr<RtspMessage>, int> RtspMessage::Parse(const std::shared_ptr<const ov::Data> &data)
{
	auto rtsp_message = std::make_shared<RtspMessage>();

	auto parsed_length = rtsp_message->ParseInternal(data);

	return {parsed_length > 0 ? rtsp_message : nullptr, parsed_length};
}

int RtspMessage::ParseInternal(const std::shared_ptr<const ov::Data> &data)
{
	auto string = data->GetDataAs<char>();
	size_t string_len = data->GetLength();

	auto header_length = ParseHeaderLength(string, string_len);
	if(header_length <= 0)
	{
		// Not enough data
		return 0;
	}

	if(ParseHeader(string, header_length) == false)
	{
		// Error
		return -1;
	}

	_is_header_data_uptodate = false;

	auto cseq = GetHeaderField(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::CSeq));
	if(cseq == nullptr)
	{
		logte("There is no CSeq field in the received packet");
		return -1;
	}

	_cseq = cseq->GetValueAsInteger();

	auto content_length_field = GetHeaderField(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::ContentLength));
	if(content_length_field == nullptr)
	{
		return header_length;
	}

	auto content_length = content_length_field->GetValueAsInteger();

	if(string_len < static_cast<size_t>(header_length + content_length))
	{
		// Not enough data
		return 0;
	}
	
	SetBody(data->Subdata(header_length, content_length));

	return header_length + content_length;
}

bool RtspMessage::ParseHeader(const char *string, size_t string_len)
{
	// Validate
	if((string[string_len-4] == '\r' && string[string_len-3] == '\n' && 
		string[string_len-2] == '\r' && string[string_len-1] == '\n') == false)
	{
		return false;
	}

	// Elemenates last '\r\n\r\n'
	ov::String header(string, string_len - 4);

	auto lines = header.Split("\r\n");
	bool first = true;
	for(const auto &line : lines)
	{
		// first line
		if(first == true)
		{
			first = false;

			// Response
			// Status-Line =   RTSP-Version SP Status-Code SP Reason-Phrase CRLF
			// RTSP-Version = "RTSP" "/" 1*DIGIT "." 1*DIGIT
			if(line.HasPrefix("RTSP/") == true)
			{
				auto tokens = line.Split(" ");
				if(tokens.size() < 3)
				{
					logte("Could not parse status-line : %s", line.CStr());
					return false;
				}
				_type = RtspMessageType::RESPONSE;
				_rtsp_version = tokens[0];
				_status_code = std::atoi(tokens[1].CStr());
				_reason_phrase = line.Substring(tokens[0].GetLength() + tokens[1].GetLength() + 2);
				logtt("Parsed status line : version(%s) status code(%d) reason phrase(%s)", _rtsp_version.CStr(), _status_code, _reason_phrase.CStr());
			}
			// Request
			// Request-Line = Method SP Request-URI SP RTSP-Version CRLF
			else
			{
				auto tokens = line.Split(" ");
				if(tokens.size() != 3)
				{
					logte("Could not parse request-line : %s", line.CStr());
					return false;
				}

				_type = RtspMessageType::REQUEST;
				_method_string = tokens[0];
				_request_uri = tokens[1];
				_rtsp_version = tokens[2];

				logti("Parsed request line : method(%s) uri(%s) version(%s)", _method_string.CStr(), _request_uri.CStr(), _rtsp_version.CStr());		
			}
		}
		else
		{
			auto field = RtspHeaderFieldParser::Parse(line);
			if(field == nullptr)
			{
				logtw("Could not parse header field (%s)", line.CStr());
				continue;
			}

			logtt("Pased header field : %s", field->Serialize().CStr());
			AddHeaderField(field);
		}
	}

	return true;
}

int RtspMessage::ParseHeaderLength(const char *string, size_t string_len)
{
	if(string_len < 4)
	{
		return 0;
	}

	for(size_t i=0; i<string_len-3; i++)
	{
		if(string[i] == '\r' && string[i+1] == '\n' && string[i+2] == '\r' && string[i+3] == '\n')
		{
			// index + 1 (start from zero)
			return i+3 + 1;
		}
	}

	return 0;
}

RtspMessage::RtspMessage(RtspMethod method, uint32_t cseq, ov::String request_uri)
	: RtspMessage(RtspMethodToString(method), cseq, request_uri)
{
	
}

RtspMessage::RtspMessage(ov::String method, uint32_t cseq, ov::String request_uri)
{
	_rtsp_version = RTSP_VERSION;
	_type = RtspMessageType::REQUEST;
	_method_string = method;
	_request_uri = request_uri;
	_cseq = cseq;

	// CSeq
	// https://tools.ietf.org/html/rfc2326#section-12.17
	// This field MUST be present in all requests and responses.
	AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::CSeq, _cseq));
}

RtspMessage::RtspMessage(uint32_t status_code, uint32_t cseq, ov::String reason_phrase)
{
	_rtsp_version = RTSP_VERSION;
	_type = RtspMessageType::RESPONSE;
	_status_code = status_code;
	_reason_phrase = reason_phrase;
	_cseq = cseq;
	
	// CSeq
	// https://tools.ietf.org/html/rfc2326#section-12.17
	// This field MUST be present in all requests and responses.
	AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::CSeq, _cseq));
}

// TODO(Getroot): I have to deal with this because there are multiple header fields with the same name. 
// For example, currently only the first of WWW-Authenticate is processed.
bool RtspMessage::AddHeaderField(const std::shared_ptr<RtspHeaderField> &field)
{
	_header_fields.emplace(field->GetName().UpperCaseString(), field);
	_is_header_data_uptodate = false;

	return true;
}

std::shared_ptr<RtspHeaderField> RtspMessage::GetHeaderField(ov::String field_name) const
{
	auto it = _header_fields.find(field_name.UpperCaseString());
	if(it == _header_fields.end())
	{
		return nullptr;
	}

	return it->second;
}

bool RtspMessage::DelHeaderField(ov::String field_name)
{
	_header_fields.erase(field_name.UpperCaseString());

	_is_header_data_uptodate = false;

	return true;
}

bool RtspMessage::SetBody(const std::shared_ptr<const ov::Data> &body_data)
{
	_body = body_data;

	// Update "Content-length" field
	
	// https://tools.ietf.org/html/rfc2326#section-12.14
	// Unlike HTTP, it MUST be included in all messages that carry content beyond the header
	// portion of the message. If it is missing, a default value of zero is
	// assumed. It is interpreted according to [H14.14].

	if(_body != nullptr && _body->GetLength() > 0)
	{
		DelHeaderField(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::ContentLength));
		
		auto field = std::make_shared<RtspHeaderField>();
		field->SetContent(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::ContentLength), body_data->GetLength());
		AddHeaderField(field);

		_is_header_data_uptodate = false;
	}

	return true;
}

bool RtspMessage::SerializeHeader()
{
	ov::String header;

	if(_type == RtspMessageType::REQUEST)
	{
		//<Method> <URI> RTSP/1.0\r\n
		header.Format("%s %s RTSP/%s\r\n", _method_string.CStr(), _request_uri.CStr(), _rtsp_version.CStr());
	}
	else if(_type == RtspMessageType::RESPONSE)
	{
		// RTSP/1.0 <Status code> <Reason phrase>\r\n
		header.Format("RTSP/%s %d %s", _rtsp_version.CStr(), _status_code, _reason_phrase.CStr());
	}
	else
	{
		return false;
	}

	// Header fields
	for(const auto &it : _header_fields)
	{
		auto field = it.second;

		header.Append(field->Serialize());
		header.Append("\r\n");
	}

	header.Append("\r\n");

	_header = header.ToData(false);

	_is_header_data_uptodate = true;

	return true;
}

std::shared_ptr<ov::Data> RtspMessage::GetHeader()
{
	if(_is_header_data_uptodate == false)
	{
		// remake header data
		if(SerializeHeader() == false)
		{
			return nullptr;
		}
	}

	return _header;
}

std::shared_ptr<const ov::Data> RtspMessage::GetBody()
{
	return _body;
}

std::shared_ptr<ov::Data> RtspMessage::GetMessage()
{
	auto message = std::make_shared<ov::Data>();

	auto header = GetHeader();
	if(header == nullptr)
	{
		return nullptr;
	}

	message->Append(header);
	
	auto body = GetBody();
	if(body != nullptr)
	{
		message->Append(body);
	}

	return message;
}

RtspMessageType RtspMessage::GetMessageType() const
{
	return _type;
}

ov::String RtspMessage::GetMethodStr() const
{
	return _method_string;
}

ov::String RtspMessage::GetRequestUri() const
{
	return _request_uri;
}

uint32_t RtspMessage::GetCSeq() const
{
	return _cseq;
}

uint32_t RtspMessage::GetStatusCode() const
{
	return _status_code;
}

ov::String RtspMessage::GetReasonPhrase() const
{
	return _reason_phrase;
}

ov::String RtspMessage::DumpHeader() const
{
	ov::String dump;
	if(GetMessageType() == RtspMessageType::REQUEST)
	{
		dump.AppendFormat("Request: %s %s %s\n", _method_string.CStr(), _request_uri.CStr(), _rtsp_version.CStr());
	}
	else
	{
		dump.AppendFormat("Response: %s %d %s\n", _rtsp_version.CStr(), _status_code, _reason_phrase.CStr());
	}

	for(const auto &it : _header_fields)
	{
		auto field = it.second;
		dump.AppendFormat("%s\n", field->Serialize().CStr());
	}

	return dump;
}