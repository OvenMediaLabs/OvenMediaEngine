//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//
//==============================================================================

#include "simulcast_attr.h"

RidAttr::RidAttr()
{
}
RidAttr::~RidAttr()
{
}

void RidAttr::SetId(const ov::String &id)
{
	_id = id;
}
const ov::String &RidAttr::GetId() const
{
	return _id;
}

void RidAttr::SetDirection(Direction dir)
{
	_direction = dir;
}
const RidAttr::Direction RidAttr::GetDirection() const
{
	return _direction;
}

void RidAttr::SetState(State state)
{
	_state = state;
}
const RidAttr::State RidAttr::GetState() const
{
	return _state;
}

void RidAttr::SetPts(const std::vector<uint16_t> &pts)
{
	_pts = pts;

	ov::String pt_list_str;
	for (const auto &pt : _pts)
	{
		if (pt_list_str.IsEmpty() == false)
		{
			pt_list_str += ",";
		}
		pt_list_str += ov::Converter::ToString(pt);
	}

	_restrictions["pt"] = pt_list_str;
}
void RidAttr::AddPt(const uint16_t &pt)
{
	_pts.push_back(pt);

	auto existing = _restrictions.find("pt");
	if (existing != _restrictions.end())
	{
		existing->second += ",";
		existing->second += ov::Converter::ToString(pt);
	}
	else
	{
		_restrictions["pt"] = ov::Converter::ToString(pt);
	}
}
std::optional<uint16_t> RidAttr::GetFirstPt() const
{
	if (_pts.empty())
	{
		return std::nullopt;
	}

	return _pts.front();
}
const std::vector<uint16_t> &RidAttr::GetPts() const
{
	return _pts;
}

void RidAttr::SetMaxWidth(uint16_t width)
{
	_max_width = width;
	_restrictions["max-width"] = ov::Converter::ToString(width);
}
uint16_t RidAttr::GetMaxWidth() const
{
	return _max_width;
}

void RidAttr::SetMaxHeight(uint16_t height)
{
	_max_height = height;
	_restrictions["max-height"] = ov::Converter::ToString(height);
}
uint16_t RidAttr::GetMaxHeight() const
{
	return _max_height;
}

void RidAttr::SetMaxFrameRate(float framerate)
{
	_max_framerate = framerate;
	_restrictions["max-fps"] = ov::Converter::ToString(framerate);
}
float RidAttr::GetMaxFrameRate() const
{
	return _max_framerate;
}

void RidAttr::SetMaxBitrate(uint32_t bitrate)
{
	_max_bitrate = bitrate;
	_restrictions["max-br"] = ov::Converter::ToString(bitrate);
}

uint32_t RidAttr::GetMaxBitrate() const
{
	return _max_bitrate;
}

void RidAttr::AddRestriction(const ov::String &key, const ov::String &value)
{
	auto key_lower = key.LowerCaseString();
	_restrictions[key_lower] = value;

	if (key == "pt")
	{
		// 23,24,25
		auto pt_list = value.Split(",");
		for (const auto &pt : pt_list)
		{
			_pts.push_back(ov::Converter::ToUInt32(pt.CStr()));
		}
	}
	else if (key == "max-width")
	{
		_max_width = ov::Converter::ToUInt32(value.CStr());
	}
	else if (key == "max-height")
	{
		_max_height = ov::Converter::ToUInt32(value.CStr());
	}
	else if (key == "max-fps")
	{
		_max_framerate = ov::Converter::ToFloat(value.CStr());
	}
	else if (key == "max-br")
	{
		_max_bitrate = ov::Converter::ToUInt32(value.CStr());
	}
}
const std::map<ov::String, ov::String> &RidAttr::GetRestrictions() const
{
	return _restrictions;
}

ov::String RidAttr::ToString() const
{
	ov::String str;
	str.Format("a=rid:%s %s", _id.CStr(), _direction == Direction::Send ? "send" : "recv");

	if (_restrictions.empty() == false)
	{
		str += " ";

		ov::String restriction_str;
		for (auto item : _restrictions)
		{
			if (restriction_str.IsEmpty() == false)
			{
				restriction_str += ";";
			}

			restriction_str += item.first;
			restriction_str += "=";
			restriction_str += item.second;
			restriction_str += ";";
		}

		str += restriction_str;
	}
	
	return str;
}

SimulcastLayer::SimulcastLayer()
{
}
SimulcastLayer::~SimulcastLayer()
{
}

void SimulcastLayer::SetRidList(const std::vector<std::shared_ptr<RidAttr>> &rid_list)
{
	_rid_list = rid_list;
}

void SimulcastLayer::AddRid(const std::shared_ptr<RidAttr> &rid)
{
	_rid_list.push_back(rid);
}

std::shared_ptr<RidAttr> SimulcastLayer::GetFirstRid() const
{
	if (_rid_list.empty())
	{
		return nullptr;
	}

	return _rid_list.front();
}

std::shared_ptr<RidAttr> SimulcastLayer::GetRid(const ov::String &id) const
{
	for (auto &rid : _rid_list)
	{
		if (rid->GetId() == id)
		{
			return rid;
		}
	}

	return nullptr;
}

const std::vector<std::shared_ptr<RidAttr>> &SimulcastLayer::GetRidList() const
{
	return _rid_list;
}
