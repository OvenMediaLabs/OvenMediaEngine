//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "web_socket_frame.h"

#include <algorithm>

#include "./web_socket_private.h"

// Currently, OME does not handle websocket frame larger than 1 MB
#define MAX_WEBSOCKET_FRAME_SIZE (1024LL * 1024LL)

#define WEB_SOCKET_PROCESS(step, func)                            \
	do                                                            \
	{                                                             \
		if (_last_status == step)                                 \
		{                                                         \
			consumed_bytes = func(input_data);                    \
                                                                  \
			if (consumed_bytes < 0)                               \
			{                                                     \
				_last_status = FrameParseStatus::Error;           \
				*read_bytes = consumed_bytes;                     \
				return false;                                     \
			}                                                     \
                                                                  \
			if (consumed_bytes > 0)                               \
			{                                                     \
				input_data = input_data->Subdata(consumed_bytes); \
				total_consumed_bytes += consumed_bytes;           \
			}                                                     \
		}                                                         \
	} while (false)

namespace http
{
	namespace prot
	{
		namespace ws
		{
			Frame::Frame()
			{
				_previous_data = std::make_shared<ov::Data>(sizeof(_header) + sizeof(uint64_t) + sizeof(_frame_masking_key));
			}

			bool Frame::ParseData(const std::shared_ptr<const ov::Data> &input_data, void *output_data, size_t size_to_parse, ssize_t *read_bytes)
			{
				if (_previous_data->IsEmpty())
				{
					auto input_length = input_data->GetLength();
					if (input_length >= size_to_parse)
					{
						// input_data can fill the output_data
						::memcpy(output_data, input_data->GetData(), size_to_parse);
						*read_bytes = size_to_parse;
						return true;
					}

					// Need more data

					if (_previous_data->Append(input_data))
					{
						*read_bytes = input_length;
					}
					else
					{
						// Could not append data
						*read_bytes = -1;
					}

					if (_previous_data->GetLength() >= MAX_WEBSOCKET_FRAME_SIZE)
					{
						// Too many data - stop read
						*read_bytes = -1;
					}

					// output_data isn't filled
					return false;
				}

				// Append input_data to _previous_data to fill output_data.

				// It should always be smaller than size_to_parse because there is insufficient data left in the existing step
				OV_ASSERT2(_previous_data->GetLength() < size_to_parse);

				auto remained_size = size_to_parse - _previous_data->GetLength();
				auto size_to_read = std::min(remained_size, input_data->GetLength());

				if (_previous_data->Append(input_data->GetData(), size_to_read) == false)
				{
					*read_bytes = -1;
					return false;
				}

				*read_bytes = size_to_read;

				if (_previous_data->GetLength() == size_to_parse)
				{
					// output_data can be filled
					::memcpy(output_data, _previous_data->GetData(), size_to_parse);
					_previous_data->SetLength(0);
					return true;
				}
				else
				{
					// Need more data
					OV_ASSERT2(size_to_read == input_data->GetLength());
				}

				// output_data isn't filled
				return false;
			}

			bool Frame::Process(const std::shared_ptr<const ov::Data> &data, ssize_t *read_bytes)
			{
				switch (_last_status)
				{
					case FrameParseStatus::Completed:
						[[fallthrough]];
					case FrameParseStatus::Error:
						OV_ASSERT(false, "Invalid last status: %d, %p", _last_status, this);
						// If parsing has already been completed or an error has occurred, if data comes in again, it is considered an error
						_last_status = FrameParseStatus::Error;
						*read_bytes = -1;
						return false;

					default:
						break;
				}

				auto input_data = data;
				size_t total_consumed_bytes = 0;

				while (input_data->GetLength() > 0)
				{
					ssize_t consumed_bytes = 0;

					WEB_SOCKET_PROCESS(FrameParseStatus::ParseHeader, ProcessHeader);
					WEB_SOCKET_PROCESS(FrameParseStatus::ParseLength, ProcessLength);
					WEB_SOCKET_PROCESS(FrameParseStatus::ParseMask, ProcessMask);
					WEB_SOCKET_PROCESS(FrameParseStatus::ParsePayload, ProcessPayload);

					if (_last_status == FrameParseStatus::Completed)
					{
						break;
					}
				}

				*read_bytes = total_consumed_bytes;
				return (_last_status == FrameParseStatus::Completed);
			}  // namespace ws

			FrameParseStatus Frame::GetStatus() const noexcept
			{
				return _last_status;
			}

			const FrameHeader &Frame::GetHeader() const noexcept
			{
				return _header;
			}

			ssize_t Frame::ProcessHeader(const std::shared_ptr<const ov::Data> &data)
			{
				ssize_t consumed_bytes;

				if (ParseData(data, &_header, sizeof(_header), &consumed_bytes))
				{
					// Handle extensions flag
					// TODO(dimiden) - Extensions are now considered unused (need to be implemented later)
					bool extensions = false;

					if ((extensions == false) && (_header.reserved != 0x00))
					{
						consumed_bytes = -1;
						logte("Invalid reserved value: %d (expected: %d)", _header.reserved, 0x00);
					}
					else
					{
						_last_status = FrameParseStatus::ParseLength;
					}
				}

				return consumed_bytes;
			}

			ssize_t Frame::ProcessLength(const std::shared_ptr<const ov::Data> &data)
			{
				ssize_t consumed_bytes;

				// Calculate payload length
				//
				// RFC6455 - 5.2.  Base Framing Protocol
				//
				// Payload length:  7 bits, 7+16 bits, or 7+64 bits
				//
				//     The length of the "Payload data", in bytes: if 0-125, that is the
				//     payload length.  If 126, the following 2 bytes interpreted as a
				//     16-bit unsigned integer are the payload length.  If 127, the
				//     following 8 bytes interpreted as a 64-bit unsigned integer (the
				//     most significant bit MUST be 0) are the payload length.  Multibyte
				//     length quantities are expressed in network byte order.  Note that
				//     in all cases, the minimal number of bytes MUST be used to encode
				//     the length, for example, the length of a 124-byte-long string
				//     can't be encoded as the sequence 126, 0, 124.  The payload length
				//     is the length of the "Extension data" + the length of the
				//     "Application data".  The length of the "Extension data" may be
				//     zero, in which case the payload length is the length of the
				//     "Application data".
				switch (_header.payload_length)
				{
					case 126: {
						// If payload_length is 126, the next 16 bits are payload length
						uint16_t extra_length;

						if (ParseData(data, &extra_length, sizeof(extra_length), &consumed_bytes))
						{
							_payload_length = ov::NetworkToHost16(extra_length);
							_remained_payload_length = _payload_length;
						}
						else
						{
							// Need more data or an error occurred
							return consumed_bytes;
						}

						break;
					}

					case 127: {
						// If payload_length is 127, the next 64 bits are payload length
						uint64_t extra_length;

						if (ParseData(data, &extra_length, sizeof(extra_length), &consumed_bytes))
						{
							_payload_length = ov::NetworkToHost64(extra_length);
							_remained_payload_length = _payload_length;
						}
						else
						{
							// Need more data or an error occurred
							return consumed_bytes;
						}

						break;
					}

					default:
						_payload_length = _header.payload_length;
						_remained_payload_length = _payload_length;

						consumed_bytes = 0;
						break;
				}

				// To optimize the speed, masking is processed with a multiple of 8, which is set to a multiple of 8 in advance to prevent buffer reallocation
				ssize_t total_length = _header.mask ? (((_payload_length + 7L) / 8L) * 8L) : _payload_length;
				_payload = std::make_shared<ov::Data>(total_length);

				_last_status = FrameParseStatus::ParseMask;

				return consumed_bytes;
			}

			ssize_t Frame::ProcessMask(const std::shared_ptr<const ov::Data> &data)
			{
				ssize_t consumed_bytes;

				if (_header.mask)
				{
					if (ParseData(data, &_frame_masking_key, sizeof(_frame_masking_key), &consumed_bytes))
					{
						_last_status = FrameParseStatus::ParsePayload;
					}
					else
					{
						// Need more data or an error occurred
					}
				}
				else
				{
					consumed_bytes = 0;
					_last_status = FrameParseStatus::ParsePayload;
				}

				return consumed_bytes;
			}

			ssize_t Frame::ProcessPayload(const std::shared_ptr<const ov::Data> &data)
			{
				// All of data in _previous_data were used in the previous step
				OV_ASSERT2(_previous_data->GetLength() == 0);

				size_t bytes_to_read = std::min(data->GetLength(), static_cast<size_t>(_remained_payload_length));

				if (bytes_to_read > 0)
				{
					if (_payload->Append(data->GetData(), bytes_to_read) == false)
					{
						return -1;
					}
				}

				_remained_payload_length -= bytes_to_read;

				if (_remained_payload_length == 0)
				{
					// Frame is completed
					OV_ASSERT2(_payload->GetLength() == _payload_length);

					if (_header.mask)
					{
						size_t payload_length = _payload->GetLength();

						// For quick calculation, we'll adjust it to a multiple of 8
						// (When _header.mask is on, it has already been allocated as a multiple of 8 when allocating data)

						static_assert(sizeof(_frame_masking_key) == 4, "sizeof(_frame_masking_key) must be 4 bytes");

						uint64_t mask = static_cast<uint64_t>(_frame_masking_key) << (sizeof(_frame_masking_key) * 8) | static_cast<uint64_t>(_frame_masking_key);

						size_t block_count = ((payload_length + 7L) / 8L);

						auto current = _payload->GetWritableDataAs<uint64_t>();

						for (size_t index = 0; index < block_count; index++)
						{
							*current ^= mask;
							current++;
						}
					}

					logtt("The frame is finished: %s", ToString().CStr());

					_last_status = FrameParseStatus::Completed;
				}
				else
				{
					// Need more data
				}

				return bytes_to_read;
			}

			void Frame::Reset()
			{
				::memset(&_header, 0, sizeof(_header));
				_header_read_bytes = 0;

				_remained_payload_length = 0UL;
				_payload_length = 0UL;
				_frame_masking_key = 0U;

				_last_status = FrameParseStatus::ParseHeader;

				_previous_data->SetLength(0);
				_payload = nullptr;
			}

			ov::String Frame::ToString() const
			{
				return ov::String::FormatString(
					"<WebSocketFrame: %p, Fin: %s, Opcode: %d, Mask: %s, Payload length: %ld",
					this,
					(_header.fin ? "True" : "False"),
					_header.opcode,
					(_header.mask ? "True" : "False"),
					_payload_length);
			}
		}  // namespace ws
	}	   // namespace svr
}  // namespace http
