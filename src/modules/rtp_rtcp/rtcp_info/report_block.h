#pragma once
#include "base/ovlibrary/ovlibrary.h"

#define RTCP_REPORT_BLOCK_SIZE	24

class ReportBlock
{
public:
	ReportBlock(){}
	ReportBlock(uint32_t src_ssrc, uint8_t fraction_lost, uint32_t cumulative_lost,
					uint32_t extended_highest_sequence_num, uint32_t jitter, uint32_t lsr, uint32_t dlsr);
	static std::shared_ptr<ReportBlock> Parse(const uint8_t *data, size_t data_size);

	uint32_t	GetSrcSsrc(){return _src_ssrc;}
	uint8_t		GetFractionLost(){return _fraction_lost;}
	uint32_t	GetCumulativeLost(){return _cumulative_lost;}
	uint32_t	GetExtendedHighestSequenceNum(){return _extended_highest_sequence_num;}
	uint32_t	GetJitter(){return _jitter;}
	uint32_t	GetLastSr(){return _last_sr;}
	uint32_t	GetDelaySinceLastSr(){return _delay_since_last_sr;}

	void SetSrcSsrc(uint32_t src_ssrc){_src_ssrc = src_ssrc;}
	void SetFractionLost(uint8_t fraction_lost){_fraction_lost = fraction_lost;}
	void SetCumulativeLost(uint32_t cumulative_list){_cumulative_lost = cumulative_list;}
	void SetExtendedHighestSequenceNum(uint32_t num){_extended_highest_sequence_num = num;}
	void SetJitter(uint32_t jitter){_jitter = jitter;}
	void SetLSR(uint32_t last_sr){_last_sr = last_sr;}
	void SetDLSR(uint32_t delay_last_sr){_delay_since_last_sr = delay_last_sr;}

	std::shared_ptr<ov::Data> GetData();

	void Print();

private:
	uint32_t	_src_ssrc = 0; 
	uint8_t		_fraction_lost = 0;		// 8bits
	uint32_t	_cumulative_lost = 0;	// 24bits
	uint32_t	_extended_highest_sequence_num = 0; // 32bits
	uint32_t	_jitter = 0; // 32bits
	uint32_t	_last_sr = 0; // 32bits (NTP timestamp), If no SR has been received yet, the field is set to zero.
	uint32_t	_delay_since_last_sr = 0; // 32bits, 1/65536 seconds
};