/*
 * We referenced this code from WEBRTC NATIVE CODE.

[WebRTC Native Code License]

Copyright (c) 2011, The WebRTC project authors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

  * Neither the name of Google nor the names of its contributors may
    be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include "rtp_packet.h"
#include "rtp_packetizing_manager.h"
#include <deque>
#include <memory>
#include <queue>
#include <string>

const size_t kNalHeaderSize = 1;
const size_t kFuAHeaderSize = 2;
const size_t kLengthFieldSize = 2;

enum NalDefs : uint8_t { kFBit = 0x80, kNriMask = 0x60, kTypeMask = 0x1F };
enum FuDefs : uint8_t { kSBit = 0x80, kEBit = 0x40, kRBit = 0x20 };

class RtpPacketizerH264 : public RtpPacketizingManager 
{
public:
	RtpPacketizerH264();
	~RtpPacketizerH264() override;

	size_t SetPayloadData(size_t max_payload_len, size_t last_packet_reduction_len, const RTPVideoTypeHeader *rtp_type_header, FrameType frame_type,
							const uint8_t* payload_data, size_t payload_size, const FragmentationHeader* fragmentation) override;

	bool NextPacket(RtpPacket* rtp_packet) override;

private:
	struct Fragment 
	{
		Fragment(const uint8_t* buffer, size_t length)
			: buffer(buffer), length(length) 
		{
		}
		Fragment(const Fragment& fragment)
			: buffer(fragment.buffer), length(fragment.length) 
		{
		}
		~Fragment() = default;

		const uint8_t* buffer = nullptr;
		size_t length = 0;
	};

	struct PacketUnit 
	{
		PacketUnit(const Fragment& source_fragment,
		           bool first_fragment,
		           bool last_fragment,
		           bool aggregated,
		           uint8_t header)
			: source_fragment(source_fragment),
			  first_fragment(first_fragment),
			  last_fragment(last_fragment),
			  aggregated(aggregated),
			  header(header) {}

		const Fragment source_fragment;
		bool first_fragment;
		bool last_fragment;
		bool aggregated;
		uint8_t header;
	};

	bool GeneratePackets();
	void PacketizeFuA(size_t fragment_index);
	size_t PacketizeStapA(size_t fragment_index);
	bool IsAggregationPossible(size_t fragment_index) const;
	bool PacketizeSingleNalu(size_t fragment_index);
	void NextSingleUnitPacket(RtpPacket* rtp_packet);
	void NextAggregatePacket(RtpPacket* rtp_packet, bool last);
	void NextFragmentPacket(RtpPacket* rtp_packet);

	size_t _max_payload_len;
	size_t _last_packet_reduction_len;
	size_t _num_packets_left;
	H26XPacketizationMode _packetization_mode;
	std::deque<Fragment> _input_fragments;
	std::queue<PacketUnit> _packets;
};