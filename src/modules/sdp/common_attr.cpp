//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "common_attr.h"

#include <regex>

#include "sdp_regex_pattern.h"

CommonAttr::CommonAttr()
{
}

CommonAttr::~CommonAttr()
{
}

bool CommonAttr::SerializeCommonAttr(ov::String &sdp)
{
	sdp = "";

	// EXTMAP-ALLOW-MIXED
	if (_extmap_allow_mixed)
	{
		sdp.AppendFormat("a=extmap-allow-mixed\r\n");
	}

	// FINGERPRINT
	if (!_fingerprint_algorithm.IsEmpty() && !_fingerprint_value.IsEmpty())
	{
		sdp.AppendFormat("a=fingerprint:%s %s\r\n",
						 _fingerprint_algorithm.CStr(), _fingerprint_value.CStr());
	}

	// ICE-OPTION
	if (!_ice_option.IsEmpty())
	{
		sdp.AppendFormat("a=ice-options:%s\r\n", _ice_option.CStr());
	}

	// ice-ufrag, ice-password
	if (!_ice_ufrag.IsEmpty() && !_ice_pwd.IsEmpty())
	{
		sdp.AppendFormat(
			"a=ice-ufrag:%s\r\n"
			"a=ice-pwd:%s\r\n",
			_ice_ufrag.CStr(),
			_ice_pwd.CStr());
	}

	// ICE candidates
	for (auto &candidate : _ice_candidates)
	{
		sdp.AppendFormat("a=%s\r\n", candidate->ToString().CStr());
	}

	if (_ice_candidates.empty() == false)
	{
		sdp.AppendFormat("a=end-of-candidates\r\n");
	}

	// control
	if (!_control.IsEmpty())
	{
		sdp.AppendFormat("a=control:%s\r\n", _control.CStr());
	}

	return true;
}

bool CommonAttr::ParsingCommonAttrLine(char type, std::string content)
{
	std::smatch matches;

	// a=extmap-allow-mixed
	if (content.compare(0, OV_COUNTOF("extmap-allow-mixed") - 1, "extmap-allow-mixed") == 0)
	{
		_extmap_allow_mixed = true;
	}
	// a=fingerprint:sha-256 D7:81:CF:01:46:FB:2D
	else if (content.compare(0, OV_COUNTOF("fi") - 1, "fi") == 0)
	{
		/*
		if(std::regex_search(content, matches, std::regex("^fingerprint:(\\S*) (\\S*)")))
		{
			_fingerprint_algorithm = std::string(matches[1]).c_str();
			_fingerprint_value = std::string(matches[2]).c_str();
		}
		*/
		auto match = SDPRegexPattern::GetInstance()->MatchFingerprint(content.c_str());
		if (match.GetGroupCount() != 2 + 1)
		{
			return false;
		}

		_fingerprint_algorithm = match.GetGroupAt(1).GetValue();
		_fingerprint_value = match.GetGroupAt(2).GetValue();
	}
	// a=ice-options:trickle
	else if (content.compare(0, OV_COUNTOF("ice-o") - 1, "ice-o") == 0)
	{
		/*
		if(std::regex_search(content, matches, std::regex("^ice-options:(\\S*)")))
		{
			_ice_option = std::string(matches[1]).c_str();
		}
		*/

		auto match = SDPRegexPattern::GetInstance()->MatchIceOption(content.c_str());
		if (match.GetGroupCount() != 1 + 1)
		{
			return false;
		}

		_ice_option = match.GetGroupAt(1).GetValue();
	}
	// a=ice-ufrag:0dfa46c9
	else if (content.compare(0, OV_COUNTOF("ice-u") - 1, "ice-u") == 0)
	{
		/*
		if(std::regex_search(content, matches, std::regex("^ice-ufrag:(\\S*)")))
		{
			_ice_ufrag = std::string(matches[1]).c_str();
		}
		*/
		auto match = SDPRegexPattern::GetInstance()->MatchIceUfrag(content.c_str());
		if (match.GetGroupCount() != 1 + 1)
		{
			return false;
		}

		_ice_ufrag = match.GetGroupAt(1).GetValue();
	}
	else if (content.compare(0, OV_COUNTOF("ice-p") - 1, "ice-p") == 0)
	{
		/*
		if(std::regex_search(content, matches, std::regex("^ice-pwd:(\\S*)")))
		{
			_ice_pwd = std::string(matches[1]).c_str();
		}
		*/
		auto match = SDPRegexPattern::GetInstance()->MatchIcePwd(content.c_str());
		if (match.GetGroupCount() != 1 + 1)
		{
			return false;
		}

		_ice_pwd = match.GetGroupAt(1).GetValue();
	}
	else if (content.compare(0, OV_COUNTOF("cand") - 1, "cand") == 0)
	{
		// candidate:1 1 UDP 2130706431 198.51.100.1 39132 typ host
		auto candidate = std::make_shared<IceCandidate>();
		if (candidate->ParseFromString(content.c_str()) == true)
		{
			AddIceCandidate(candidate);
		}
	}
	else if (content.compare(0, OV_COUNTOF("con") - 1, "con") == 0)
	{
		/*
		if(std::regex_search(content, matches, std::regex("^control:(\\S*)")))
		{
			_control = std::string(matches[1]).c_str();
		}
		*/
		auto match = SDPRegexPattern::GetInstance()->MatchControl(content.c_str());
		if (match.GetGroupCount() != 1 + 1)
		{
			return false;
		}

		_control = match.GetGroupAt(1).GetValue();
	}
	else if (content.compare(0, OV_COUNTOF("set") - 1, "set") == 0)
	{
		// a=setup:actpass
		/*
		if(std::regex_search(content, matches, std::regex("^setup:(\\w*)")))
		{
			if(matches.size() != 1 + 1)
			{
				parsing_error = true;
				break;
			}

			SetSetup(std::string(matches[1]).c_str());
		}
		*/
		auto match = SDPRegexPattern::GetInstance()->MatchSetup(content.c_str());
		if (match.GetGroupCount() != 1 + 1)
		{
			return false;
		}

		SetSetup(match.GetGroupAt(1).GetValue());
	}
	// a=end-of-candidates
	else if (content.compare(0, OV_COUNTOF("end-of") - 1, "end-of") == 0)
	{
		// Nothing to do
	}
	else
	{
		logw("SDP", "Unknown attribute type: %c=%s", type, content.c_str());
	}

	return true;
}

// a=extmap-allow-mixed
void CommonAttr::SetExtmapAllowMixed(const bool allow)
{
	_extmap_allow_mixed = allow;
}

bool CommonAttr::GetExtmapAllowMixed() const
{
	return _extmap_allow_mixed;
}

// a=fingerprint:sha-256 D7:81:CF:01:46:FB:2D
//TODO(getroot): algorithm을 enum값으로 변경
void CommonAttr::SetFingerprint(const ov::String &algorithm, const ov::String &value)
{
	_fingerprint_algorithm = algorithm;
	_fingerprint_value = value;
}

ov::String CommonAttr::GetFingerprintAlgorithm() const
{
	return _fingerprint_algorithm;
}

ov::String CommonAttr::GetFingerprintValue() const
{
	return _fingerprint_value;
}

// a=ice-options:trickle
//TODO(getroot): option을 enum값으로 변경
void CommonAttr::SetIceOption(const ov::String &option)
{
	_ice_option = option;
}

ov::String CommonAttr::GetIceOption() const
{
	return _ice_option;
}

// a=ice-pwd:c32d4070c67e9782bea90a9ab46ea838
void CommonAttr::SetIceUfrag(const ov::String &ufrag)
{
	_ice_ufrag = ufrag;
}

ov::String CommonAttr::GetIceUfrag() const
{
	return _ice_ufrag;
}

// a=ice-ufrag:0dfa46c9
void CommonAttr::SetIcePwd(const ov::String &pwd)
{
	_ice_pwd = pwd;
}

ov::String CommonAttr::GetIcePwd() const
{
	return _ice_pwd;
}

void CommonAttr::SetControl(const ov::String &control)
{
	_control = control;
}

ov::String CommonAttr::GetControl() const
{
	return _control;
}

// a=setup:actpass
void CommonAttr::SetSetup(const SetupType type)
{
	_setup = type;
	switch (_setup)
	{
		case SetupType::Active:
			_setup_str = "active";
			break;
		case SetupType::Passive:
			_setup_str = "passive";
			break;
		case SetupType::ActPass:
			_setup_str = "actpass";
			break;
		default:
			_setup_str = "actpass";	 // default value
	}
}

CommonAttr::SetupType CommonAttr::GetSetup() const
{
	return _setup;
}

ov::String CommonAttr::GetSetupStr() const
{
	return _setup_str;
}

bool CommonAttr::SetSetup(const ov::String &type)
{
	if (type.UpperCaseString() == "ACTIVE")
	{
		SetSetup(SetupType::Active);
	}
	else if (type.UpperCaseString() == "PASSIVE")
	{
		SetSetup(SetupType::Passive);
	}
	else if (type.UpperCaseString() == "ACTPASS")
	{
		SetSetup(SetupType::ActPass);
	}
	else
	{
		OV_ASSERT(false, "Invalid setup type: %s", type.CStr());
		return false;
	}

	return true;
}

std::vector<std::shared_ptr<IceCandidate>> CommonAttr::GetIceCandidates() const
{
	return _ice_candidates;
}

void CommonAttr::AddIceCandidate(std::shared_ptr<IceCandidate> ice_candidate)
{
	_ice_candidates.push_back(ice_candidate);
}
