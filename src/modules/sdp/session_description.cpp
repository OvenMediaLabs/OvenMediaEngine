/*
 *
 * The REGEX pattern in this source code is refers to libsdptransform.
 * https://github.com/ibc/libsdptransform [MIT LICENSE]
 *
 *
 */

#include "session_description.h"
#include <sstream>
#include <regex>

#include "sdp_regex_pattern.h"

SessionDescription::SessionDescription(const SdpType &type)
	: _type(type)
{
}

SessionDescription::~SessionDescription()
{
}

void SessionDescription::Release()
{
	_media_list.clear();
}

SessionDescription::SdpType SessionDescription::GetType() const
{
	return _type;
}

bool SessionDescription::UpdateData(ov::String &sdp)
{
	// Session
	sdp.Format(
		"v=%d\r\n"
		"o=%s %u %d %s IP%d %s\r\n"
		"s=%s\r\n"
		"t=%d %d\r\n",
		_version,
		_user_name.CStr(), _session_id, _session_version, _net_type.CStr(), _ip_version, _address.CStr(),
		_session_name.CStr(),
		_start_time, _stop_time
	);

	// Append all media lines to a=group:BUNDLE (Currently, OME only supports BUNDLE only)
	sdp += "a=group:BUNDLE";

	for(auto &media_description : _media_list)
	{
		sdp.AppendFormat(" %s", media_description->GetMid().value_or("").CStr());
	}

	sdp += "\r\n";
	sdp += "a=group:LS";
	for(auto &media_description : _media_list)
	{
		sdp.AppendFormat(" %s", media_description->GetMid().value_or("").CStr());
	}

	/* 
	Deprecated 
	
	sdp += "\r\n";
	sdp += "a=group:LS";

	for(auto &media_description : _media_list)
	{
		sdp.AppendFormat(" %s", media_description->GetMid().CStr());
	}
	*/

	sdp += "\r\n";

	// msid-semantic
	if(_msid_semantic.IsEmpty() == false)
	{
		sdp.AppendFormat("a=msid-semantic:%s %s\r\n", _msid_semantic.CStr(), _msid_token.CStr());
	}

	// Common Attributes
	ov::String common_attr_text;

	if(SerializeCommonAttr(common_attr_text) == false)
	{
		return false;
	}

	sdp += common_attr_text;

	// Media
	for(auto &media_description : _media_list)
	{
		sdp += media_description->ToString();
	}

	return true;
}

bool SessionDescription::FromString(const ov::String &sdp)
{
	static const std::regex ValidLineRegex("^([a-z])=(.*)");
	std::stringstream sdpstream(sdp.CStr());
	std::string line;

	ov::String media_desc_sdp;
	bool media_level = false;

	while(std::getline(sdpstream, line, '\n'))
	{
		if(line.size() && line[line.length() - 1] == '\r')
		{
			line.pop_back();
		}

		if(!std::regex_search(line, ValidLineRegex))
		{
			continue;
		}
		char type = line[0];
		std::string content = line.substr(2);

		if(type == 'm')
		{
			if(media_level == true)
			{
				auto media_desc = std::make_shared<MediaDescription>();
				if(media_desc->FromString(media_desc_sdp) == false)
				{
					return false;
				}
				AddMedia(media_desc);
			}

			media_desc_sdp.Clear();
			media_desc_sdp.AppendFormat("%s\n", line.c_str());
			media_level = true;
		}
		else if(media_level == true)
		{
			media_desc_sdp.AppendFormat("%s\n", line.c_str());

			if(sdpstream.rdbuf()->in_avail() == 0)
			{
				auto media_desc = std::make_shared<MediaDescription>();
				if(media_desc->FromString(media_desc_sdp) == false)
				{
					return false;
				}
				AddMedia(media_desc);
			}
		}
		else
		{
			if(ParsingSessionLine(type, content) == false)
			{
				return false;
			}
		}
	}

	Update();

	return true;
}

bool SessionDescription::ParsingSessionLine(char type, std::string content)
{
	bool parsing_error = false;
	std::smatch matches;

	switch(type)
	{
		case 'v':
			/*
			// v=0
			if(std::regex_search(content, matches, std::regex("^(\\d*)$")))
			{
				if(matches.size() != 1 + 1)
				{
					parsing_error = true;
					break;
				}
				SetVersion(static_cast<uint8_t>(std::stoi(matches[1])));
			}
			*/
			{
				auto match = SDPRegexPattern::GetInstance()->MatchVersion(content.c_str());
				if(match.GetGroupCount() != 1 + 1)
				{
					parsing_error = true;
					break;
				}

				SetVersion(ov::Converter::ToInt32(match.GetGroupAt(1).GetValue().CStr()));
			}
			break;
		case 'o':
			// o=OvenMediaEngine 1882243660 2 IN IP4 127.0.0.1
			/*
			if(std::regex_search(content, matches, std::regex("^(\\S*) (\\d*) (\\d*) (\\S*) IP(\\d) (\\S*)")))
			{
				if(matches.size() != 6 + 1)
				{
					parsing_error = true;
					break;
				}

				SetOrigin(std::string(matches[1]).c_str(),
				          static_cast<uint32_t>(std::stoul(matches[2])),
				          static_cast<uint32_t>(std::stoul(matches[3])),
				          std::string(matches[4]).c_str(),
				          static_cast<uint8_t>(std::stoul(matches[5])),
				          std::string(matches[6]).c_str());
			}
			*/
			{
				auto match = SDPRegexPattern::GetInstance()->MatchOrigin(content.c_str());
				if(match.GetGroupCount() != 6 + 1)
				{
					parsing_error = true;
					break;
				}

				SetOrigin(
						match.GetGroupAt(1).GetValue(), // user name
						ov::Converter::ToUInt32(match.GetGroupAt(2).GetValue().CStr()), // session id
						ov::Converter::ToUInt32(match.GetGroupAt(3).GetValue().CStr()), // session version
						match.GetGroupAt(4).GetValue(), // net_type
						ov::Converter::ToInt32(match.GetGroupAt(5).GetValue().CStr()), // ip_version
						match.GetGroupAt(6).GetValue() // address
						);
			}

			break;
		case 's':
			// s=-
			/*
			if(std::regex_search(content, matches, std::regex("^(.*)")))
			{
				if(matches.size() != 1 + 1)
				{
					parsing_error = true;
					break;
				}

				SetSessionName(std::string(matches[1]).c_str());
			}*/
			{
				auto match = SDPRegexPattern::GetInstance()->MatchSessionName(content.c_str());
				if(match.GetGroupCount() != 1 + 1)
				{
					parsing_error = true;
					break;
				}
				
				SetSessionName(match.GetGroupAt(1).GetValue());
			}
			break;
		case 't':
			// t=0 0
			/*
			if(std::regex_search(content, matches, std::regex("^(\\d*) (\\d*)")))
			{
				if(matches.size() != 2 + 1)
				{
					parsing_error = true;
					break;
				}

				SetTiming(static_cast<uint32_t>(std::stoul(matches[1])),
				          static_cast<uint32_t>(std::stoul(matches[2])));
			}
			*/
			{
				auto match = SDPRegexPattern::GetInstance()->MatchTiming(content.c_str());
				if(match.GetGroupCount() != 2 + 1)
				{
					parsing_error = true;
					break;
				}
				
				SetTiming(
					ov::Converter::ToUInt32(match.GetGroupAt(1).GetValue().CStr()),
					ov::Converter::ToUInt32(match.GetGroupAt(2).GetValue().CStr())
				);
			}

			break;
		case 'a':
			// a=group:BUNDLE video audio ...
			if(content.compare(0, OV_COUNTOF("group:B") - 1, "group:B") == 0)
			{
				auto match = SDPRegexPattern::GetInstance()->MatchGourpBundle(content.c_str());
				if(match.GetGroupCount() != 1 + 1)
				{
					parsing_error = true;
					break;
				}
				
				auto bundles = match.GetGroupAt(1).GetValue();
				auto items = bundles.Split(" ");
				for(const auto &item : items)
				{
					_bundles.emplace_back(item);
				}
			}
			// a=group:LS video audio ...
			else if (content.compare(0, OV_COUNTOF("group:L") - 1, "group:L") == 0)
			{
				// Skip
			}
			// a=msid-semantic:WMS *
			else if(content.compare(0, OV_COUNTOF("ms") - 1, "ms") == 0)
			{
				/*
				if(std::regex_search(content, matches, std::regex(R"(^msid-semantic:\s?(\w*) (\S*))")))
				{
					if(matches.size() != 2 + 1)
					{
						parsing_error = true;
						break;
					}

					SetMsidSemantic(std::string(matches[1]).c_str(), std::string(matches[2]).c_str());
				}
				*/

				auto match = SDPRegexPattern::GetInstance()->MatchMsidSemantic(content.c_str());
				if(match.GetGroupCount() != 2 + 1)
				{
					parsing_error = true;
					break;
				}

				SetMsidSemantic(
					match.GetGroupAt(1).GetValue(),
					match.GetGroupAt(2).GetValue()
				);
			}
			else if(content.compare(0, OV_COUNTOF("sdpl") - 1, "sdpl") == 0)
			{
				/*
				if(std::regex_search(content, matches, std::regex("^sdplang:(\\S*)")))
				{
					_sdp_lang = std::string(matches[1]).c_str();
				}
				*/

				auto match = SDPRegexPattern::GetInstance()->MatchSdpLang(content.c_str());
				if(match.GetGroupCount() != 1 + 1)
				{
					parsing_error = true;
					break;
				}

				_sdp_lang = match.GetGroupAt(1).GetValue();
			}
			else if(content.compare(0, OV_COUNTOF("ran") - 1, "ran") == 0)
			{
				/*
				if(std::regex_search(content, matches, std::regex("^range:(\\S*)")))
				{
					_range = std::string(matches[1]).c_str();
				}
				*/

				auto match = SDPRegexPattern::GetInstance()->MatchRange(content.c_str());
				if(match.GetGroupCount() != 1 + 1)
				{
					parsing_error = true;
					break;
				}

				_range = match.GetGroupAt(1).GetValue();
			}
			else if(ParsingCommonAttrLine(type, content))
			{
				// Nothing to do
			}
			else
			{
				// Other attributes are ignored because they are not required.
				logt("SDP", "Unknown Attributes : %c=%s", type, content.c_str());
			}

			break;
		default:
			logt("SDP", "Unknown Attributes : %c=%s", type, content.c_str());
	}

	if(parsing_error)
	{
		logw("SDP", "Sdp parsing error : %c=%s", type, content.c_str());
	}

	return true;
}

// v=0
void SessionDescription::SetVersion(uint8_t version)
{
	_version = version;
}

uint8_t SessionDescription::GetVersion() const
{
	return _version;
}

// o=OvenMediaEngine 1882243660 2 IN IP4 127.0.0.1
void SessionDescription::SetOrigin(ov::String user_name, uint32_t session_id, uint32_t session_version,
                                   ov::String net_type, uint8_t ip_version, ov::String address)
{
	_user_name = user_name;
	_session_id = session_id;
	_session_version = session_version;
	_net_type = net_type;
	_ip_version = ip_version;
	_address = address;
}

ov::String SessionDescription::GetUserName() const
{
	return _user_name;
}

uint32_t SessionDescription::GetSessionId() const
{
	return _session_id;
}

uint32_t SessionDescription::GetSessionVersion() const
{
	return _session_version;
}

ov::String SessionDescription::GetNetType() const
{
	return _net_type;
}

uint8_t SessionDescription::GetIpVersion() const
{
	return _ip_version;
}

ov::String SessionDescription::GetAddress() const
{
	return _address;
}

// s=-
void SessionDescription::SetSessionName(ov::String session_name)
{
	_session_name = session_name;
}

ov::String SessionDescription::GetSessionName() const
{
	return _session_name;
}

// t=0 0
void SessionDescription::SetTiming(uint32_t start, uint32_t stop)
{
	_start_time = start;
	_stop_time = stop;
}

uint32_t SessionDescription::GetStartTime() const
{
	return _start_time;
}

uint32_t SessionDescription::GetStopTime() const
{
	return _stop_time;
}

// a=msid-semantic:WMS *
void SessionDescription::SetMsidSemantic(const ov::String &semantic, const ov::String &token)
{
	_msid_semantic = semantic;
	_msid_token = token;
}

ov::String SessionDescription::GetMsidSemantic() const
{
	return _msid_semantic;
}

ov::String SessionDescription::GetMsidToken() const
{
	return _msid_token;
}

void SessionDescription::SetSdpLang(const ov::String &lang)
{
	_sdp_lang = lang;
}

ov::String SessionDescription::GetSdpLang()
{
	return _sdp_lang;
}

void SessionDescription::SetRange(const ov::String &range)
{
	_range = range;
}

ov::String SessionDescription::GetRange()
{
	return _range;
}

// m=video 9 UDP/TLS/RTP/SAVPF 97
void SessionDescription::AddMedia(const std::shared_ptr<const MediaDescription> &media)
{
	if(media != nullptr)
	{
		// key는 향후 검색을 위해 사용될 수 있는 값임
		_media_list.push_back(media);
	}
}

const std::shared_ptr<const MediaDescription> SessionDescription::GetMediaByMid(const ov::String &mid) const
{
	for(auto &media : _media_list)
	{
		if(media->GetMid() == mid)
		{
			return media;
		}
	}

	return nullptr;
}

const std::vector<std::shared_ptr<const MediaDescription>> &SessionDescription::GetMediaList() const
{
	return _media_list;
}


const std::shared_ptr<const MediaDescription> SessionDescription::GetFirstMedia() const
{
	if(_media_list.empty() == false)
	{
		return _media_list.front();
	}

	return nullptr;
}

// 아래 값은 Session Level에 없으면 첫번째 m= line에서 값을 가져와야 한다.
ov::String SessionDescription::GetFingerprintAlgorithm() const
{
	ov::String value = CommonAttr::GetFingerprintAlgorithm();

	if(value.IsEmpty())
	{
		auto media = GetFirstMedia();
		if(media != nullptr)
		{
			value = media->GetFingerprintAlgorithm();
		}
	}

	return value;
}

ov::String SessionDescription::GetFingerprintValue() const
{
	ov::String value = CommonAttr::GetFingerprintValue();

	if(value.IsEmpty())
	{
		auto media = GetFirstMedia();
		if(media != nullptr)
		{
			value = media->GetFingerprintValue();
		}
	}

	return value;
}

ov::String SessionDescription::GetIceOption() const
{
	ov::String value = CommonAttr::GetIceOption();

	if(value.IsEmpty())
	{
		auto media = GetFirstMedia();
		if(media != nullptr)
		{
			value = media->GetIceOption();
		}
	}

	return value;
}

ov::String SessionDescription::GetIceUfrag() const
{
	ov::String value = CommonAttr::GetIceUfrag();

	if(value.IsEmpty())
	{
		auto media = GetFirstMedia();
		if(media != nullptr)
		{
			value = media->GetIceUfrag();
		}
	}

	return value;
}

ov::String SessionDescription::GetIcePwd() const
{
	ov::String value = CommonAttr::GetIcePwd();

	if(value.IsEmpty())
	{
		auto media = GetFirstMedia();
		if(media != nullptr)
		{
			value = media->GetIcePwd();
		}
	}

	return value;
}