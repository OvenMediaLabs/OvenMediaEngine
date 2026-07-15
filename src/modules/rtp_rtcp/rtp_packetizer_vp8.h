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
#include <queue>


// Packetizer for VP8.
class RtpPacketizerVp8 : public RtpPacketizingManager 
{
public:
	// Initialize with Payload from encoder.
	// The payload_data must be exactly one encoded VP8 frame.
	RtpPacketizerVp8();

	virtual ~RtpPacketizerVp8();

	size_t SetPayloadData(size_t max_payload_len, size_t last_packet_reduction_len, const RTPVideoTypeHeader *rtp_type_header, FrameType frame_type, 
							const uint8_t* payload_data, size_t payload_size, const FragmentationHeader* fragmentation) override;

	// Get the next Payload with VP8 payload header.
	// Write Payload and set marker bit of the |packet|.
	// Returns true on success, false otherwise.
	bool NextPacket(RtpPacket* packet) override;

private:
	typedef struct 
	{
		size_t payload_start_pos;
		size_t size;
		bool first_packet;
	} InfoStruct;

	typedef std::queue<InfoStruct> InfoQueue;

	static const int kXBit = 0x80;
	static const int kNBit = 0x20;
	static const int kSBit = 0x10;
	static const int kPartIdField = 0x0F;
	static const int kKeyIdxField = 0x1F;
	static const int kIBit = 0x80;
	static const int kLBit = 0x40;
	static const int kTBit = 0x20;
	static const int kKBit = 0x10;
	static const int kYBit = 0x20;

	// Calculate all packet sizes and load to packet info queue.
	int GeneratePackets();

	// Splits given part of Payload to packets with a given capacity. The last
	// packet should be reduced by last_packet_reduction_len_.
	void GeneratePacketsSplitPayloadBalanced(size_t payload_len,
		size_t capacity);

	// Insert packet into packet queue.
	void QueuePacket(size_t start_pos, size_t packet_size, bool first_packet);

	// Write the payload header and copy the Payload to the buffer.
	// The info in packet_info determines which part of the Payload is written
	// and what to write in the header fields.
	int WriteHeaderAndPayload(const InfoStruct& packet_info, uint8_t* buffer, size_t buffer_length) const;

	// Write the X field and the appropriate extension fields to buffer.
	// The function returns the extension length (including X field), or -1
	// on error.
	int WriteExtensionFields(uint8_t* buffer, size_t buffer_length) const;

	// Set the I bit in the x_field, and write PictureID to the appropriate
	// position in buffer. The function returns 0 on success, -1 otherwise.
	int WritePictureIDFields(uint8_t* x_field, uint8_t* buffer, size_t buffer_length, size_t* extension_length) const;

	// Set the L bit in the x_field, and write Tl0PicIdx to the appropriate
	// position in buffer. The function returns 0 on success, -1 otherwise.
	int WriteTl0PicIdxFields(uint8_t* x_field, uint8_t* buffer, size_t buffer_length, size_t* extension_length) const;

	// Set the T and K bits in the x_field, and write TID, Y and KeyIdx to the
	// appropriate position in buffer. The function returns 0 on success,
	// -1 otherwise.
	int WriteTIDAndKeyIdxFields(uint8_t* x_field, uint8_t* buffer, size_t buffer_length, size_t* extension_length) const;

	// Write the PictureID from codec_specific_info_ to buffer. One or two
	// bytes are written, depending on magnitude of PictureID. The function
	// returns the number of bytes written.
	int WritePictureID(uint8_t* buffer, size_t buffer_length) const;

	// Calculate and return length (octets) of the variable header fields in
	// the next header (i.e., header length in addition to vp8_header_bytes_).
	size_t PayloadDescriptorExtraLength() const;

	// Calculate and return length (octets) of PictureID field in the next
	// header. Can be 0, 1, or 2.
	size_t PictureIdLength() const;

	// Check whether each of the optional fields will be included in the header.
	bool XFieldPresent() const;
	bool TIDFieldPresent() const;
	bool KeyIdxFieldPresent() const;
	bool TL0PicIdxFieldPresent() const;
	bool PictureIdPresent() const { return (PictureIdLength() > 0); }

	const uint8_t* payload_data_;
	size_t payload_size_;
	const size_t vp8_fixed_payload_descriptor_bytes_;  // Length of VP8 Payload
	// descriptors' fixed part.
	RTPVideoHeaderVP8 hdr_info_;
	size_t max_payload_len_;
	size_t last_packet_reduction_len_;
	InfoQueue packets_;
};