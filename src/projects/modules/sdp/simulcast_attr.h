//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//
//==============================================================================

#pragma once

#include <base/ovlibrary/ovlibrary.h>

// a=rid:1 send pt=97,98;max-width=1280;max-height=720
// a=rid:2 send pt=97,98;max-width=320;max-height=180
// a=rid:3 send pt=99;max-width=320;max-height=180
// a=simulcast:send 1;2,3

class RidAttr
{
public:
	RidAttr();
	virtual ~RidAttr();

	void SetId(const ov::String &id);
	const ov::String &GetId() const;

	enum class Direction
	{
		Unknown,
		Send,
		Recv
	};

	enum class State
	{
		Active,
		Inactive	// a=simulcast: send 1,~2,~3;4 recv 4, OME does not support inactive, it will be removed
	};

	void SetDirection(Direction dir);
	const Direction GetDirection() const;

	void SetState(State state);
	const State GetState() const;

	// comma separated list of alternative RTP streams
	void SetPts(const std::vector<uint16_t> &pts);
	void AddPt(const uint16_t &pt);
	std::optional<uint16_t> GetFirstPt() const;
	const std::vector<uint16_t> &GetPts() const;

	void SetMaxWidth(uint16_t width);
	uint16_t GetMaxWidth() const;

	void SetMaxHeight(uint16_t height);
	uint16_t GetMaxHeight() const;

	void SetMaxFrameRate(float framerate);
	float GetMaxFrameRate() const;

	void SetMaxBitrate(uint32_t bitrate);
	uint32_t GetMaxBitrate() const;

	void AddRestriction(const ov::String &key, const ov::String &value);
	const std::map<ov::String, ov::String> &GetRestrictions() const;

	ov::String ToString() const;

private:
	ov::String _id;
	Direction _direction = Direction::Unknown;
	State _state = State::Active;
	std::vector<uint16_t> _pts;
	uint16_t _max_width = 0;
	uint16_t _max_height = 0;
	float _max_framerate = 0.0f;
	uint32_t _max_bitrate = 0;
	// Unsupport for max-fs, max-br, max-pps, max-bpp ...
	std::map<ov::String, ov::String> _restrictions;
};

class SimulcastLayer
{
public:
	SimulcastLayer();
	virtual ~SimulcastLayer();

	void SetRidList(const std::vector<std::shared_ptr<RidAttr>> &rid_list);
	void AddRid(const std::shared_ptr<RidAttr> &rid);
	std::shared_ptr<RidAttr> GetFirstRid() const;
	std::shared_ptr<RidAttr> GetRid(const ov::String &id) const;
	const std::vector<std::shared_ptr<RidAttr>> &GetRidList() const;

private:
	// base rid and alternatives
	std::vector<std::shared_ptr<RidAttr>> _rid_list;
};