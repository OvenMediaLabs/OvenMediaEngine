#pragma once
#include "base/ovlibrary/ovlibrary.h"
#include "rtcp_info/rtcp_info.h"

class RtcpPacket;

// RtcpInfo base class
class RtcpInfo
{
public:
	virtual bool Parse(const RtcpPacket &packet) = 0;

	// RtcpInfo must provide packet type
	virtual RtcpPacketType GetPacketType() const = 0;

	// If the packet type is one of the feedback messages (205, 206) child must override this function
	virtual uint8_t GetCountOrFmt() const
	{
		return _count_or_fmt;
	}

	// RtcpInfo must provide raw data
	virtual std::shared_ptr<ov::Data> GetData() const = 0;
	virtual bool HasPadding() const = 0;
	virtual void DebugPrint() = 0;

	// Extra Information
	void SetRtpSsrc(uint32_t ssrc){_rtp_ssrc = ssrc;}
	uint8_t GetRtpSsrc() const {return _rtp_ssrc;}
	void SetRtspChannel(uint32_t rtsp_channel) {_rtsp_channel = rtsp_channel;}
	uint32_t GetRtspChannel() const {return _rtsp_channel;}

protected:
	void SetCount(uint8_t count) {_count_or_fmt = count;}
	void SetFmt(uint8_t fmt) {_count_or_fmt = fmt;}

private:
	uint8_t		_count_or_fmt = 0;

	// Extra Infomation
	// it is not used for RTCP packet, but in order to find RTP stream faster
	uint32_t	_rtp_ssrc = 0;
	// If it is from RTSP, rtsp_channel value is valid
	uint32_t	_rtsp_channel = 0;
};

#define RTCP_DEFAULT_MAX_PACKET_SIZE	1472

class RtcpPacket
{
public:
	// Build RTCP Packet
	bool Build(const std::shared_ptr<RtcpInfo> &info);
	// block_size : returns used bytes
	bool Parse(const uint8_t* buffer, const size_t buffer_size, size_t &block_size);
	
	std::shared_ptr<RtcpInfo> GetRtcpInfo() const {return _info;}

	RtcpPacketType GetType() const {return _type;}
	uint8_t GetReportCount() const {return _count_or_format;}
	uint8_t GetFMT() const {return _count_or_format;}
	// From Header
	uint16_t GetLengthField() const {return _length;}
	const uint8_t* GetPayload() const {return _payload;}
	size_t GetPayloadSize() const {return _payload_size;}

	// Return raw data
	const std::shared_ptr<ov::Data> GetData() const {return _data;}
	
private:
	void SetType(RtcpPacketType type) {_type = type;}
	void SetReportCount(uint8_t count) {_count_or_format = count;}
	void SetFMT(uint8_t fmt) {_count_or_format = fmt;}

	uint8_t				_version = 2; // always 2
	bool				_has_padding = false;
	size_t				_padding_size = 0;
	uint8_t				_count_or_format = 0; // depends on packet type (if the packet type is fb then this valuable will be used as format)
	RtcpPacketType		_type;
	uint16_t			_length = 0;
	uint8_t*			_payload = nullptr;
	size_t				_payload_size = 0;

	std::shared_ptr<RtcpInfo> _info = nullptr;
	std::shared_ptr<ov::Data> _data = nullptr;
};
