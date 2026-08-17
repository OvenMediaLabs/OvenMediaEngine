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


#include "rtp_packetizer_vp8.h"

RtpPacketizerVp8::RtpPacketizerVp8()
	: payload_data_(NULL),
	  payload_size_(0),
	  vp8_fixed_payload_descriptor_bytes_(1)
{
}

RtpPacketizerVp8::~RtpPacketizerVp8()
{

}

size_t RtpPacketizerVp8::SetPayloadData(size_t max_payload_len, size_t last_packet_reduction_len, const RTPVideoTypeHeader *rtp_type_header, FrameType frame_type, 
										const uint8_t *payload_data, size_t payload_size, const FragmentationHeader * /* fragmentation */)
{
	hdr_info_ = rtp_type_header->vp8;
	max_payload_len_ = max_payload_len;
	last_packet_reduction_len_ = last_packet_reduction_len;
	payload_data_ = payload_data;
	payload_size_ = payload_size;
	if(GeneratePackets() < 0)
	{
		return 0;
	}
	return packets_.size();
}

bool RtpPacketizerVp8::NextPacket(RtpPacket *packet)
{
	if(packets_.empty())
	{
		return false;
	}
	InfoStruct packet_info = packets_.front();
	packets_.pop();

	uint8_t *buffer = packet->AllocatePayload(packets_.empty() ? max_payload_len_ - last_packet_reduction_len_ : max_payload_len_);
	int bytes = WriteHeaderAndPayload(packet_info, buffer, max_payload_len_);
	if(bytes < 0)
	{
		return false;
	}
	packet->SetPayloadSize(bytes);
	packet->SetMarker(packets_.empty());
	return true;
}

int RtpPacketizerVp8::GeneratePackets()
{
	if(max_payload_len_ < vp8_fixed_payload_descriptor_bytes_ +
	                      PayloadDescriptorExtraLength() + 1 +
	                      last_packet_reduction_len_)
	{
		// The provided payload length is not long enough for the Payload
		// descriptor and one Payload byte in the last packet.
		// Return an error.
		return -1;
	}

	size_t per_packet_capacity = max_payload_len_ -
	                             (vp8_fixed_payload_descriptor_bytes_ + PayloadDescriptorExtraLength());

	GeneratePacketsSplitPayloadBalanced(payload_size_, per_packet_capacity);
	return 0;
}

void RtpPacketizerVp8::GeneratePacketsSplitPayloadBalanced(size_t payload_len, size_t capacity)
{
	// Last packet of the last partition is smaller. Pretend that it's the same
	// size, but we must write more Payload to it.
	size_t total_bytes = payload_len + last_packet_reduction_len_;
	// Integer divisions with rounding up.
	size_t num_packets_left = (total_bytes + capacity - 1) / capacity;
	size_t bytes_per_packet = total_bytes / num_packets_left;
	size_t num_larger_packets = total_bytes % num_packets_left;
	size_t remaining_data = payload_len;
	while(remaining_data > 0)
	{
		// Last num_larger_packets are 1 byte wider than the rest. Increase
		// per-packet Payload size when needed.
		if(num_packets_left == num_larger_packets)
		{
			++bytes_per_packet;
		}
		size_t current_packet_bytes = bytes_per_packet;
		if(current_packet_bytes > remaining_data)
		{
			current_packet_bytes = remaining_data;
		}
		// This is not the last packet in the whole Payload, but there's no data
		// left for the last packet. Leave at least one byte for the last packet.
		if(num_packets_left == 2 && current_packet_bytes == remaining_data)
		{
			--current_packet_bytes;
		}
		QueuePacket(payload_len - remaining_data, current_packet_bytes, remaining_data == payload_len);
		remaining_data -= current_packet_bytes;
		--num_packets_left;
	}
}

void RtpPacketizerVp8::QueuePacket(size_t start_pos, size_t packet_size, bool first_packet)
{
	// Write info to packet info struct and store in packet info queue.
	InfoStruct packet_info;
	packet_info.payload_start_pos = start_pos;
	packet_info.size = packet_size;
	packet_info.first_packet = first_packet;
	packets_.push(packet_info);
}

int RtpPacketizerVp8::WriteHeaderAndPayload(const InfoStruct &packet_info, uint8_t *buffer, size_t buffer_length) const
{
	// Write the VP8 Payload descriptor.
	//       0
	//       0 1 2 3 4 5 6 7 8
	//      +-+-+-+-+-+-+-+-+-+
	//      |X| |N|S| PART_ID |
	//      +-+-+-+-+-+-+-+-+-+
	// X:   |I|L|T|K|         | (mandatory if any of the below are used)
	//      +-+-+-+-+-+-+-+-+-+
	// I:   |PictureID (8/16b)| (optional)
	//      +-+-+-+-+-+-+-+-+-+
	// L:   |   TL0PIC_IDX    | (optional)
	//      +-+-+-+-+-+-+-+-+-+
	// T/K: |TID:Y|  KEYIDX   | (optional)
	//      +-+-+-+-+-+-+-+-+-+

	buffer[0] = 0;
	if(XFieldPresent())
	{
		buffer[0] |= kXBit;
	}

	if(hdr_info_.non_reference)
	{
		buffer[0] |= kNBit;
	}

	if(packet_info.first_packet)
	{
		buffer[0] |= kSBit;
	}

	const int extension_length = WriteExtensionFields(buffer, buffer_length);
	if(extension_length < 0)
	{
		return -1;
	}

	memcpy(&buffer[vp8_fixed_payload_descriptor_bytes_ + extension_length], &payload_data_[packet_info.payload_start_pos], packet_info.size);

	// Return total length of written data.
	return packet_info.size + vp8_fixed_payload_descriptor_bytes_ + extension_length;
}

int RtpPacketizerVp8::WriteExtensionFields(uint8_t *buffer, size_t buffer_length) const
{
	size_t extension_length = 0;
	if(XFieldPresent())
	{
		uint8_t *x_field = buffer + vp8_fixed_payload_descriptor_bytes_;
		*x_field = 0;
		extension_length = 1;  // One octet for the X field.
		if(PictureIdPresent())
		{
			if(WritePictureIDFields(x_field, buffer, buffer_length, &extension_length) < 0)
			{
				return -1;
			}
		}

		if(TL0PicIdxFieldPresent())
		{
			if(WriteTl0PicIdxFields(x_field, buffer, buffer_length, &extension_length) < 0)
			{
				return -1;
			}
		}
		if(TIDFieldPresent() || KeyIdxFieldPresent())
		{
			if(WriteTIDAndKeyIdxFields(x_field, buffer, buffer_length, &extension_length) < 0)
			{
				return -1;
			}
		}
	}
	return static_cast<int>(extension_length);
}

int RtpPacketizerVp8::WritePictureIDFields(uint8_t *x_field, uint8_t *buffer, size_t buffer_length, size_t *extension_length) const
{
	*x_field |= kIBit;

	const int pic_id_length = WritePictureID(buffer + vp8_fixed_payload_descriptor_bytes_ + *extension_length,
	                                         buffer_length - vp8_fixed_payload_descriptor_bytes_ - *extension_length);
	if(pic_id_length < 0)
	{
		return -1;
	}
	*extension_length += pic_id_length;
	return 0;
}

int RtpPacketizerVp8::WritePictureID(uint8_t *buffer, size_t buffer_length) const
{
	const uint16_t pic_id = static_cast<uint16_t>(hdr_info_.picture_id);
	size_t picture_id_len = PictureIdLength();
	if(picture_id_len > buffer_length)
	{
		return -1;
	}

	if(picture_id_len == 2)
	{
		buffer[0] = 0x80 | ((pic_id >> 8) & 0x7F);
		buffer[1] = pic_id & 0xFF;
	}
	else if(picture_id_len == 1)
	{
		buffer[0] = pic_id & 0x7F;
	}
	return static_cast<int>(picture_id_len);
}

int RtpPacketizerVp8::WriteTl0PicIdxFields(uint8_t *x_field, uint8_t *buffer, size_t buffer_length, size_t *extension_length) const
{
	if(buffer_length < vp8_fixed_payload_descriptor_bytes_ + *extension_length + 1)
	{
		return -1;
	}

	*x_field |= kLBit;
	buffer[vp8_fixed_payload_descriptor_bytes_ + *extension_length] = hdr_info_.tl0_pic_idx;
	++*extension_length;
	return 0;
}

int RtpPacketizerVp8::WriteTIDAndKeyIdxFields(uint8_t *x_field, uint8_t *buffer, size_t buffer_length, size_t *extension_length) const
{
	if(buffer_length < vp8_fixed_payload_descriptor_bytes_ + *extension_length + 1)
	{
		return -1;
	}
	uint8_t *data_field = &buffer[vp8_fixed_payload_descriptor_bytes_ + *extension_length];
	*data_field = 0;

	if(TIDFieldPresent())
	{
		*x_field |= kTBit;
		*data_field |= hdr_info_.temporal_idx << 6;
		*data_field |= hdr_info_.layer_sync ? kYBit : 0;
	}

	if(KeyIdxFieldPresent())
	{
		*x_field |= kKBit;
		*data_field |= (hdr_info_.key_idx & kKeyIdxField);
	}
	++*extension_length;
	return 0;
}

size_t RtpPacketizerVp8::PayloadDescriptorExtraLength() const
{
	size_t length_bytes = PictureIdLength();
	if(TL0PicIdxFieldPresent())
	{
		++length_bytes;
	}
	if(TIDFieldPresent() || KeyIdxFieldPresent())
	{
		++length_bytes;
	}
	if(length_bytes > 0)
	{
		++length_bytes;  // Include the extension field.
	}

	return length_bytes;
}

size_t RtpPacketizerVp8::PictureIdLength() const
{
	if(hdr_info_.picture_id == kNoPictureId)
	{
		return 0;
	}
	return 2;
}

bool RtpPacketizerVp8::XFieldPresent() const
{
	return (TIDFieldPresent() || TL0PicIdxFieldPresent() || PictureIdPresent() || KeyIdxFieldPresent());
}

bool RtpPacketizerVp8::TIDFieldPresent() const
{
	return (hdr_info_.temporal_idx != kNoTemporalIdx);
}

bool RtpPacketizerVp8::KeyIdxFieldPresent() const
{
	return (hdr_info_.key_idx != kNoKeyIdx);
}

bool RtpPacketizerVp8::TL0PicIdxFieldPresent() const
{
	return (hdr_info_.tl0_pic_idx != kNoTl0PicIdx);
}