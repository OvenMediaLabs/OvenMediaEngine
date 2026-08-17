#include "report_block.h"
#include "rtcp_private.h"

#include <base/ovlibrary/byte_io.h>

ReportBlock::ReportBlock(uint32_t src_ssrc, uint8_t fraction_lost, uint32_t cumulative_lost,
					uint32_t extended_highest_sequence_num, uint32_t jitter, uint32_t lsr, uint32_t dlsr)
{
	SetSrcSsrc(src_ssrc);
	SetFractionLost(fraction_lost);
	SetCumulativeLost(cumulative_lost);
	SetExtendedHighestSequenceNum(extended_highest_sequence_num);
	SetJitter(jitter);
	SetLSR(lsr);
	SetDLSR(dlsr);	
}

std::shared_ptr<ReportBlock> ReportBlock::Parse(const uint8_t *data, size_t data_size)
{
	auto block = std::make_shared<ReportBlock>();
	if(data_size < RTCP_REPORT_BLOCK_SIZE)
	{
		return nullptr;
	}
	
	block->_src_ssrc = ByteReader<uint32_t>::ReadBigEndian(&data[0]);
	block->_fraction_lost = ByteReader<uint8_t>::ReadBigEndian(&data[4]);
	block->_cumulative_lost = ByteReader<uint32_t, 3>::ReadBigEndian(&data[5]);
	block->_extended_highest_sequence_num = ByteReader<uint32_t>::ReadBigEndian(&data[8]);
	block->_jitter = ByteReader<uint32_t>::ReadBigEndian(&data[12]);
	block->_last_sr = ByteReader<uint32_t>::ReadBigEndian(&data[16]);
	block->_delay_since_last_sr = ByteReader<uint32_t>::ReadBigEndian(&data[20]);

	return block;
}

std::shared_ptr<ov::Data> ReportBlock::GetData()
{
	auto data = std::make_shared<ov::Data>(RTCP_REPORT_BLOCK_SIZE);
	data->SetLength(RTCP_REPORT_BLOCK_SIZE);

	ov::ByteStream write_stream(data);

	write_stream.WriteBE32(_src_ssrc);
	write_stream.WriteBE(_fraction_lost);
	write_stream.WriteBE24(_cumulative_lost);
	write_stream.WriteBE32(_extended_highest_sequence_num);
	write_stream.WriteBE32(_jitter);
	write_stream.WriteBE32(_last_sr);
	write_stream.WriteBE32(_delay_since_last_sr);

	return data;
}

/*
double RtcpPacket::DelayCalculation(uint32_t lsr, uint32_t dlsr)
{
    if(lsr == 0 || dlsr == 0)
        return 0;

    uint32_t msw = 0;
    uint32_t lsw = 0;

    ov::Clock::GetNtpTime(msw, lsw);

    uint32_t tr = ((msw & 0xFFFF) << 16) | (lsw >> 16);

    if((tr - lsr) <= dlsr)
        return 0;

    return static_cast<double>(tr - lsr - dlsr)/65536.0;
}
*/

void ReportBlock::Print()
{
	logti("Receiver Report >> source ssrc(%u) fraction lost(%u) cumulative lost(%u) highest sequence(%u) jitter(%u) last sr(%u) delay(%.2f)",
			GetSrcSsrc(), GetFractionLost(), GetCumulativeLost(), GetExtendedHighestSequenceNum(), GetJitter(), GetLastSr(), (double)GetDelaySinceLastSr()/65536.0);
}