//==============================================================================
//
//  MPEGTS SpliceInsert
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
/*
	table_id: 8 (8)
	section_syntax_indicator: 1 (9)
	private_indicator: 1 (10)
	reserved: 2 (12)
	section_length: 12 (24)
	protocol_version: 8 (32)
	encrypted_packet: 1 (33)
	encryption_algorithm: 6 (39)
	pts_adjustment: 33 (72)
	cw_index: 8 (80)
	tier: 12 (92)
	splice_command_length: 12 (104)
	splice_command_type: 8 (112)
		// SPLICE_INSERT
		splice_event_id: 32 (144)
		splice_event_cancel_indicator: 1 (145)
		reserved: 7 (152)

		out_of_network_indicator: 1 (153)
		program_splice_flag: 1 (154)
		duration_flag: 1 (155)
		splice_immediate_flag: 1 (156)
		event_id_compliance_flag: 1 (157)
		reserved: 3 (160)

		// splice_time
		time_specified_flag: 1 (161)
		reserved: 6 (167)
		pts_time: 33 (200)

		// break_duration
		auto_return: 1 (201)
		reserved: 6 (207)
		duration: 33 (240)

		unique_program_id: 16 (256)
		avail_num: 8 (264)
		avails_expected: 8 (272)

	descriptor_loop_length: 16 (288)
	CRC_32: 32 (320)
*/


#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "mpegts_splice_info.h"

namespace mpegts
{
	class SpliceInsert : public SpliceInfo
	{
	public:
		SpliceInsert(uint32_t event_id);
		~SpliceInsert() = default;

		// true : scte35-out, false : scte35-in
		void SetOutofNetworkIndicator(bool out_of_network);
		void SetPTS(int64_t pts);
		void SetDuration(uint64_t duration);
		void SetAutoReturn(bool auto_return);

		uint32_t GetSpliceEventId() const
		{
			return _splice_event_id;
		}

		uint8_t GetOutofNetworkIndicator() const
		{
			return _out_of_network_indicator;
		}

		uint8_t GetProgramSpliceFlag() const
		{
			return _program_splice_flag;
		}

		uint8_t GetDurationFlag() const
		{
			return _duration_flag;
		}

		uint8_t GetSpliceImmediateFlag() const
		{
			return _splice_immediate_flag;
		}

		uint8_t GetEventIdComplianceFlag() const
		{
			return _event_id_compliance_flag;
		}

		int64_t GetPTSTime() const
		{
			return _splice_time._pts_time;
		}

		uint64_t GetDuration() const
		{
			return _break_duration._duration;
		}
		
		bool GetAutoReturn() const
		{
			return _break_duration._auto_return != 0;
		}

		static std::shared_ptr<SpliceInsert> ParseSpliceCommand(const std::shared_ptr<const ov::Data> &data);
		ov::String ToString() const override;

	private:
		std::shared_ptr<ov::Data> BuildSpliceCommand() override;

		uint32_t _splice_event_id = 0;				 // 32bits
		uint8_t _splice_event_cancel_indicator = 0;	 // 1bit
		uint8_t _reserved = 0x07;					 // 7bits

		// cancel indicator == 0
			uint8_t _out_of_network_indicator = 0;	// 1bit, 1: scte35-out 0: scte35-in
			uint8_t _program_splice_flag = 1;		// 1bit
			uint8_t _duration_flag = 0;				// 1bit
			uint8_t _splice_immediate_flag = 0;		// 1bit
			uint8_t _event_id_compliance_flag = 0;	// 1bit
			uint8_t _reserved2 = 0x03;				// 3bits
			// (program_splice_flag == ‘1’) && (splice_immediate_flag == ‘0’)
				SpliceTime _splice_time;				// 32bits

			// duration_flag == 1
			BreakDuration _break_duration;

		uint16_t _unique_program_id = 0;  // 16bits
		uint8_t _avail_num = 0;			  // 8bits
		uint8_t _avails_expected = 0;	  // 8bits
	};
}