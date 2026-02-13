//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "rtcp_transport_cc_feedback_generator.h"

#include "../rtp_header_extension/rtp_header_extension_transport_cc.h"

#define OV_LOG_TAG "transport-cc"

RtcpTransportCcFeedbackGenerator::RtcpTransportCcFeedbackGenerator(uint8_t extension_id, uint32_t sender_ssrc)
{
	_extension_id = extension_id;
	_sender_ssrc = sender_ssrc;

	_created_time = std::chrono::system_clock::now();
	_last_report_time = _created_time;
}

bool RtcpTransportCcFeedbackGenerator::AddReceivedRtpPacket(const std::shared_ptr<RtpPacket> &packet)
{
	auto wide_sequence_number_opt = packet->GetExtension<uint16_t>(_extension_id);
	if (wide_sequence_number_opt.has_value() == false)
	{
		// There is no transport-wide sequence number in the RTP header extension
		static int log_times = 10;
		if (log_times > 0)
		{
			logtw("AddReceivedRtpPacket: There is no transport-wide sequence number in the RTP header extension : %s", packet->Dump().CStr());
			log_times--;
		}
		return false;
	}

	// Read transport-wide sequence number
	auto wide_sequence_number = wide_sequence_number_opt.value();

	logtt("AddReceivedRtpPacket: wide_seq(%u) %s", wide_sequence_number, packet->Dump().CStr());

	// Add feedback info
	int64_t delta = 0;
	uint8_t delta_size = 0;

	// first packet of feedback message
	if (_transport_cc == nullptr)
	{
		_transport_cc = std::make_shared<TransportCc>();

		_transport_cc->SetFeedbackPacketCount(_fb_pkt_count);
		_fb_pkt_count++;

		_transport_cc->SetSenderSsrc(_sender_ssrc);
		_transport_cc->SetBaseSequenceNumber(wide_sequence_number);

		// Reference time
		_last_reference_time = std::chrono::system_clock::now();

		// multiples of 64ms
		uint32_t reference_time = std::chrono::duration_cast<std::chrono::milliseconds>(_last_reference_time - _created_time).count() / 64;

		_transport_cc->SetReferenceTime(reference_time);

		// Base sequence number
		uint16_t base_sequence_number = 0;
		if (_is_first_packet == true)
		{
			_is_first_packet = false;
			base_sequence_number = wide_sequence_number;
		}
		else
		{
			if (wide_sequence_number != static_cast<uint16_t>(_last_wide_sequence_number + 1))
			{
				logtw("wide sequence number is not continuous : %u -> %u", _last_wide_sequence_number, wide_sequence_number);
			}

			base_sequence_number = _last_wide_sequence_number + 1;
		}

		_transport_cc->SetBaseSequenceNumber(base_sequence_number);

		// Delta of first packet : Decimal part in reference_time is expressed in units of 250us.
		// The integer part in reference_time is expressed in units of 64ms.
		double decimal = static_cast<double>((std::chrono::duration_cast<std::chrono::milliseconds>(_last_reference_time - _created_time).count() / 64.0)) - static_cast<double>(reference_time);

		// to multiple of 250 microseconds
		decimal = (decimal * 64 * 1000) / 250;
		delta = static_cast<int64_t>(decimal);

		_last_rtp_received_time = _last_reference_time;

		logtt("last rtp received time : %lld", std::chrono::duration_cast<std::chrono::milliseconds>(_last_rtp_received_time.time_since_epoch()).count());
	}
	else
	{
		// delta : multiple of 250us from the last rtp received time
		auto now = std::chrono::system_clock::now();
		int64_t diff = std::chrono::duration_cast<std::chrono::microseconds>(now - _last_rtp_received_time).count();
		delta = diff / 250;

		if (delta < 0)
		{
			logtw("delta is negative : %d", delta);
		}

		_last_rtp_received_time = now;

		logtt("last rtp received time : %lld, diff(%lld), delta(%d)", std::chrono::duration_cast<std::chrono::milliseconds>(_last_rtp_received_time.time_since_epoch()).count(), diff, delta);
	}

	// delta size
	// 1 : [0 ~ 63.75ms] (0 * 250us ~ 0xFF * 250us)
	// 2 : [-8192.0 ~ 8191.75ms] (-0x8000 * 250us ~ 0x7FFF * 250us)
	if (delta >= 0 && delta <= 0xFF)
	{
		delta_size = 1;
	}
	else if (delta >= -0x8000 && delta <= 0x7FFF)
	{
		delta_size = 2;
	}
	// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.5
	// If the delta exceeds even the larger limits, a new feedback
	// message must be used, where the 24-bit base receive delta can
	// cover very large gaps.

	// TODO(Getroot) : If the delta exceeds the large range,
	// a new feedback message is created and the delta of the packet is reduced
	// using the 24-bit reference time. However, this is a very inefficient
	// specification because only one delta is entered in one feedback message
	// in the worst case in a very slow situation. Temporarily, I clamped the min/max,
	// and I'll try to see if there is a way to improve this.
	else if (delta < -0x8000)
	{
		delta = -0x8000;
		delta_size = 2;
	}
	else if (delta > 0x7FFF)
	{
		delta = 0x7FFF;
		delta_size = 2;
	}

	_transport_cc->AddPacketFeedbackInfo(std::make_shared<TransportCc::PacketFeedbackInfo>(wide_sequence_number, true, delta_size, delta));

	_last_wide_sequence_number = wide_sequence_number;
	_last_media_ssrc = packet->Ssrc();

	return true;
}

bool RtcpTransportCcFeedbackGenerator::HasElapsedSinceLastTransportCc(uint32_t milliseconds)
{
	auto now = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_report_time).count();

	if (elapsed >= milliseconds)
	{
		return true;
	}

	return false;
}

std::shared_ptr<RtcpPacket> RtcpTransportCcFeedbackGenerator::GenerateTransportCcMessage()
{
	if (_transport_cc == nullptr)
	{
		return nullptr;
	}

	_transport_cc->SetMediaSsrc(_last_media_ssrc);

	logtt("Generate Transport CC message : Sender SSRC(%u), Media SSRC(%u), Base Sequence Number(%u), Reference Time(%u), Packet Feedback Count(%u)",
		  _transport_cc->GetSenderSsrc(), _transport_cc->GetMediaSsrc(), _transport_cc->GetBaseSequenceNumber(), _transport_cc->GetReferenceTime(), _transport_cc->GetPacketStatusCount());

	auto rtcp_packet = std::make_shared<RtcpPacket>();
	rtcp_packet->Build(_transport_cc);

	_last_report_time = std::chrono::system_clock::now();

	_transport_cc.reset();

	return rtcp_packet;
}