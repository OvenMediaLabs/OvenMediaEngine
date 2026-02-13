#include "rtcp_sr_generator.h"
#include "sender_report.h"

RtcpSRGenerator::RtcpSRGenerator(uint32_t ssrc, uint32_t codec_rate)
{
    _ssrc = ssrc;
	_codec_rate = codec_rate;
    _created_time = std::chrono::system_clock::now();
    _last_generated_time = std::chrono::system_clock::now();
}

void RtcpSRGenerator::AddRTPPacketInfo(const std::shared_ptr<RtpPacket> &rtp_packet)
{
    _packet_count ++;
    _octec_count += rtp_packet->PayloadSize();
	_last_timestamp = rtp_packet->Timestamp();
	_last_ntptime = rtp_packet->NTPTimestamp();
}

bool RtcpSRGenerator::IsAvailableRtcpSRPacket() const
{
	return _packet_count > 0 ? true : false;
}

std::shared_ptr<RtcpPacket> RtcpSRGenerator::PopRtcpSRPacket()
{
	if(_packet_count <= 0)
	{
		return nullptr; 
	}
	
	auto report = std::make_shared<SenderReport>();
	
	uint32_t msw = 0;
	uint32_t lsw = 0;

	msw = _last_ntptime >> 32;
	lsw = _last_ntptime & 0xFFFFFFFF;

	// logc("DEBUG", "[SR] Rate(%d) Timestamp(%u) Clock(%lf) ipart(%lf) fraction(%lf) msw(%u) lsw(%u) reverse(%u)",  _codec_rate, rtp_packet.Timestamp(), clock, ipart, fraction, msw, lsw, reverse);
	
	report->SetSenderSsrc(_ssrc);
	report->SetMsw(msw);
	report->SetLsw(lsw);
	
	report->SetTimestamp(_last_timestamp);
	report->SetPacketCount(_packet_count);
	report->SetOctetCount(_octec_count);

	auto rtcp_packet = std::make_shared<RtcpPacket>();
	rtcp_packet->Build(report);

	// Reset RTCP information
	_packet_count = 0;
	_octec_count = 0;
	_last_generated_time = std::chrono::system_clock::now();
	_rtcp_generated_count++;

    return rtcp_packet;
}

uint32_t RtcpSRGenerator::GetElapsedTimeMSFromCreated()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - _created_time).count();
}

uint32_t RtcpSRGenerator::GetElapsedTimeMSFromRtcpSRGenerated()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - _last_generated_time).count();
}