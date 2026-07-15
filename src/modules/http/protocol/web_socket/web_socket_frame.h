//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace http
{
	namespace prot
	{
		namespace ws
		{
			enum class FrameOpcode : uint8_t
			{
				// *  %x0 denotes a continuation frame
				Continuation = 0x00,
				// *  %x1 denotes a text frame
				Text = 0x01,
				// *  %x2 denotes a binary frame
				Binary = 0x02,
				// *  %x3-7 are reserved for further non-control frames
				// *  %x8 denotes a connection close
				ConnectionClose = 0x08,
				// *  %x9 denotes a ping
				Ping = 0x09,
				// *  %xA denotes a pong
				Pong = 0x0A,
				// *  %xB-F are reserved for further control frames
			};

#pragma pack(push, 1)
			// RFC6455 - 5.2. Base Framing Protocol
			//  0                   1                   2                   3
			//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			// +-+-+-+-+-------+-+-------------+-------------------------------+
			// |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
			// |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
			// |N|V|V|V|       |S|             |   (if payload len==126/127)   |
			// | |1|2|3|       |K|             |                               |
			// +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
			// |     Extended payload length continued, if payload len == 127  |
			// + - - - - - - - - - - - - - - - +-------------------------------+
			// |                               |Masking-key, if MASK set to 1  |
			// +-------------------------------+-------------------------------+
			// | Masking-key (continued)       |          Payload Data         |
			// +-------------------------------- - - - - - - - - - - - - - - - +
			// :                     Payload Data continued ...                :
			// + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
			// |                     Payload Data continued ...                |
			// +---------------------------------------------------------------+
			struct FrameHeader
			{
				// === First byte ===
				// Opcode:  4 bits
				//
				//    Defines the interpretation of the "Payload data".  If an unknown
				//    opcode is received, the receiving endpoint MUST _Fail the
				//    WebSocket Connection_.  The following values are defined.
				uint8_t opcode : 4;

				// RSV1, RSV2, RSV3:  1 bit each
				//
				//    MUST be 0 unless an extension is negotiated that defines meanings
				//    for non-zero values.  If a nonzero value is received and none of
				//    the negotiated extensions defines the meaning of such a nonzero
				//    value, the receiving endpoint MUST _Fail the WebSocket
				//    Connection_.
				uint8_t reserved : 3;

				// FIN:  1 bit
				//
				//    Indicates that this is the final fragment in a message.  The first
				//    fragment MAY also be the final fragment.
				bool fin : 1;

				// === Second byte ===
				// Payload length:  7 bits, 7+16 bits, or 7+64 bits
				//
				//    The length of the "Payload data", in bytes: if 0-125, that is the
				//    payload length.  If 126, the following 2 bytes interpreted as a
				//    16-bit unsigned integer are the payload length.  If 127, the
				//    following 8 bytes interpreted as a 64-bit unsigned integer (the
				//    most significant bit MUST be 0) are the payload length.  Multibyte
				//    length quantities are expressed in network byte order.  Note that
				//    in all cases, the minimal number of bytes MUST be used to encode
				//    the length, for example, the length of a 124-byte-long string
				//    can't be encoded as the sequence 126, 0, 124.  The payload length
				//    is the length of the "Extension data" + the length of the
				//    "Application data".  The length of the "Extension data" may be
				//    zero, in which case the payload length is the length of the
				//    "Application data".
				uint8_t payload_length : 7;

				// Mask:  1 bit
				//
				//    Defines whether the "Payload data" is masked.  If set to 1, a
				//    masking key is present in masking-key, and this is used to unmask
				//    the "Payload data" as per Section 5.3.  All frames sent from
				//    client to server have this bit set to 1.
				bool mask : 1;
			};
#pragma pack(pop)

			enum class FrameParseStatus
			{
				ParseHeader,
				ParseLength,
				ParseMask,
				ParsePayload,
				Completed,
				Error,
			};

			class Frame
			{
			public:
				Frame();

				const std::shared_ptr<const ov::Data> GetPayload() const noexcept
				{
					return _payload;
				}

				// @returns true if frame is completed, false if not
				bool Process(const std::shared_ptr<const ov::Data> &data, ssize_t *read_bytes);
				FrameParseStatus GetStatus() const noexcept;

				const FrameHeader &GetHeader() const noexcept;

				void Reset();

				ov::String ToString() const;

			protected:
				// This function determines whether _previous_data and input_data have as much data as size_to_parse.
				// If there is sufficient data, output_data is filled with the data and true is returned.
				//
				// If this function returns false, we must verify that the read_bytes value is negative to check if an error has occurred
				bool ParseData(const std::shared_ptr<const ov::Data> &input_data, void *output_data, size_t size_to_parse, ssize_t *read_bytes);

				ssize_t ProcessHeader(const std::shared_ptr<const ov::Data> &data);
				ssize_t ProcessLength(const std::shared_ptr<const ov::Data> &data);
				ssize_t ProcessMask(const std::shared_ptr<const ov::Data> &data);
				ssize_t ProcessPayload(const std::shared_ptr<const ov::Data> &data);

				FrameHeader _header;
				int _header_read_bytes = 0;

				uint64_t _remained_payload_length = 0UL;
				uint64_t _payload_length = 0UL;
				uint32_t _frame_masking_key = 0U;

				FrameParseStatus _last_status = FrameParseStatus::ParseHeader;

				// Temporary buffer used only in steps ParseHeader, ParseLength, and ParseMask
				std::shared_ptr<ov::Data> _previous_data;

				std::shared_ptr<ov::Data> _payload;
			};
		}  // namespace ws
	} // namespace prot
}  // namespace http
