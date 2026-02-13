//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <modules/containers/mpegts/scte35/mpegts_splice_info.h>

/*
Data Structure

SpliceCommandType : 8 bits
ID : 32 bits
OutOfNetworkIndicator : 8 bit
Timestamp : 64 bits / 90000Hz, only positive value, maximum 33 bits (1FF FFFF FFFF)
Duration : 64 bits / 90000Hz, only positive value, maximum 33 bits (1FF FFFF FFFF)
autoReturn : 8 bit
scteData : variable
*/

class Scte35Event
{
public:
	static std::shared_ptr<Scte35Event> Create(mpegts::SpliceCommandType splice_command_type, uint32_t id, bool out_of_network, int64_t timestamp_msec, int64_t duration_msec, bool auto_return);
	static std::shared_ptr<Scte35Event> Create(const std::shared_ptr<const mpegts::SpliceInfo> &splice_info);
	static std::shared_ptr<Scte35Event> Parse(const std::shared_ptr<const ov::Data> &data);

	Scte35Event(mpegts::SpliceCommandType splice_command_type, uint32_t id, bool out_of_network, int64_t timestamp_msec, int64_t duration_msec, bool auto_return);
	~Scte35Event() = default;

	std::shared_ptr<ov::Data> Serialize() const;

	// Getter
	mpegts::SpliceCommandType GetSpliceCommandType() const;
	uint32_t GetID() const;
	bool IsOutOfNetwork() const;
	int64_t GetTimestampMsec() const;
	int64_t GetDurationMsec() const;
	bool IsAutoReturn() const;

	std::shared_ptr<ov::Data> MakeScteData() const;

	ov::String ToString() const;
private:
	mpegts::SpliceCommandType _splice_command_type;
	uint32_t _id = 0;
	bool _out_of_network = true;
	int64_t _timestamp_msec = 0;
	int64_t _duration_msec = 0;
	bool _auto_return = false;
};