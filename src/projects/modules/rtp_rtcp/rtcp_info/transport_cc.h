#pragma once
#include "base/ovlibrary/ovlibrary.h"
#include "rtcp_info.h"
#include "../rtcp_packet.h"

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
// 
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|  FMT=15 |    PT=205     |           length              |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                     SSRC of packet sender                     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                      SSRC of media source                     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      base sequence number     |      packet status count      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                 reference time                | fb pkt. count |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          packet chunk         |         packet chunk          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// .                                                               .
// .                                                               .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |         packet chunk          |  recv delta   |  recv delta   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// .                                                               .
// .                                                               .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           recv delta          |  recv delta   | zero padding  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

#define MIN_TRANSPORT_CC_RTCP_SIZE (4 + 4 + 2 + 2 + 3 + 1)
#define PACKET_CHUNK_BYTES 2

class TransportCc : public RtcpInfo
{
public:
	struct PacketFeedbackInfo
	{
		// constructor
		PacketFeedbackInfo() = default;
		PacketFeedbackInfo(uint16_t sequence_number, bool received, uint8_t delta_size = 0, int32_t received_delta = 0)
			: _wide_sequence_number(sequence_number),
			_received(received),
			_delta_size(delta_size),
			_received_delta(received_delta)
		{
		}

		uint16_t _wide_sequence_number = 0;
		bool _received = false;
		uint8_t _delta_size = 0; // 0, 1, 2, 3
		int32_t _received_delta = 0;	// 1/4000 scale
	};

	///////////////////////////////////////////
	// Implement RtcpInfo virtual functions
	///////////////////////////////////////////
	bool Parse(const RtcpPacket &header) override;
	// RtcpInfo must provide raw data
	std::shared_ptr<ov::Data> GetData() const override;
	void DebugPrint() override;

	// RtcpInfo must provide packet type
	RtcpPacketType GetPacketType() const override
	{
		return RtcpPacketType::RTPFB;
	}

	// If the packet type is one of the feedback messages (205, 206) child must provide fmt(format)
	uint8_t GetCountOrFmt() const override
	{
		return static_cast<uint8_t>(RTPFBFMT::TRANSPORT_CC);
	}

	bool HasPadding() const override
	{
		return false;
	}
	
	// Common Feedback
	uint32_t GetSenderSsrc(){return _sender_ssrc;}
	void SetSenderSsrc(uint32_t ssrc){_sender_ssrc = ssrc;}
	uint32_t GetMediaSsrc(){return _media_ssrc;}
	void SetMediaSsrc(uint32_t ssrc){_media_ssrc = ssrc;}

	// Transport Feedback
	uint16_t GetBaseSequenceNumber(){return _base_sequence_number;}
	int32_t GetReferenceTime(){return _reference_time;}
	uint16_t GetPacketStatusCount(){return _packet_status_count;}

	// Setter
	void SetBaseSequenceNumber(uint16_t base_sequence_number)
	{
		_base_sequence_number = base_sequence_number;
		_next_sequence_number = base_sequence_number;	
	}
	void SetReferenceTime(int32_t reference_time){_reference_time = reference_time;}

	void SetFeedbackPacketCount(uint8_t sequence_number)
	{
		_fb_packet_count = sequence_number;
	}

	// Add PacketFeedbackInfo
	bool AddPacketFeedbackInfo(const std::shared_ptr<PacketFeedbackInfo> &packet_feedback_info);

	// 0 ~ StatusCount - 1
	std::shared_ptr<const PacketFeedbackInfo> GetPacketFeedbackInfo(uint32_t index)
	{
		if (index >= _packet_status_count)
		{
			return nullptr;
		}
		return _packet_feedbacks[index];
	}

	// Get _packet_feedbacks
	const std::vector<std::shared_ptr<PacketFeedbackInfo>> &GetPacketFeedbacks()
	{
		return _packet_feedbacks;
	}

private:
	uint8_t GetDeltaSize(uint8_t symbol)
	{
		if (symbol == 1)
		{
			return 1;
		}
		else if (symbol == 2)
		{
			return 2;
		}
		
		return 0;
	}

	bool CountSymbolContinuity(uint32_t feedbacks_index, uint16_t &run_length, uint16_t &one_bit_symbol_length) const;

	uint32_t _sender_ssrc = 0;
	uint32_t _media_ssrc = 0;

	uint16_t _base_sequence_number = 0;
	uint16_t _next_sequence_number = 0;
	uint16_t _packet_status_count = 0;
	int32_t _reference_time = 0; // 24bit signed integer (64/1000 scale, multiples of 64ms)
	uint8_t _fb_packet_count = 0;

	mutable bool _has_padding = false;

	std::vector<std::shared_ptr<PacketFeedbackInfo>> _packet_feedbacks;
};