//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http2_frame.h"

#include "../http_protocol_private.h"

namespace http
{
	namespace prot
	{
		namespace h2
		{
			Http2Frame::Http2Frame()
			{
			}

			Http2Frame::Http2Frame(uint32_t stream_id)
			{
				SetStreamId(stream_id);
			}

			Http2Frame::Http2Frame(const std::shared_ptr<const Http2Frame> &frame)
			{
				_state = frame->_state;
				_type = frame->_type;
				_stream_id = frame->_stream_id;
				_length = frame->_length;
				_flags = frame->_flags;
				_payload = frame->_payload;
			}

			ssize_t Http2Frame::AppendData(const std::shared_ptr<const ov::Data> &data)
			{
				ssize_t consumed_bytes = 0;
				auto input_data = data;

				if (_state == Http2Frame::ParsingState::Init)
				{
					// Need Haeder Data
					auto need_header_bytes = HTTP2_FRAME_HEADER_SIZE - _packet_buffer.GetLength();
					if (input_data->GetLength() >= need_header_bytes)
					{
						consumed_bytes += need_header_bytes;
						_packet_buffer.Append(input_data->GetData(), need_header_bytes);
						input_data = input_data->Subdata(need_header_bytes);

						// Parse Header
						if (ParseHeader() == false)
						{
							_state = Http2Frame::ParsingState::Error;
							return -1;
						}

						if (_length == 0)
						{
							_state = Http2Frame::ParsingState::Completed;
							return consumed_bytes;
						}
					}
					else
					{
						consumed_bytes += input_data->GetLength();
						_packet_buffer.Append(input_data);
						// Need more data
					}
				}

				if (_state == Http2Frame::ParsingState::HeaderParsed)
				{
					// packet_buffer contains header data
					auto stored_payload_size = _packet_buffer.GetLength() - HTTP2_FRAME_HEADER_SIZE;
					// nessesary size of payload
					auto need_payload_bytes = GetLength() - stored_payload_size;
					if (input_data->GetLength() >= need_payload_bytes)
					{
						consumed_bytes += need_payload_bytes;
						_packet_buffer.Append(input_data->GetData(), need_payload_bytes);
					}
					else
					{
						consumed_bytes += input_data->GetLength();
						_packet_buffer.Append(input_data);

						// Need more data to complete Frame
					}

					if (_packet_buffer.GetLength() - HTTP2_FRAME_HEADER_SIZE == GetLength())
					{
						_payload = _packet_buffer.Subdata(HTTP2_FRAME_HEADER_SIZE);
						_state = Http2Frame::ParsingState::Completed;
					}
				}

				return consumed_bytes;
			}

			bool Http2Frame::ParseHeader()
			{
				if (_packet_buffer.GetLength() < HTTP2_FRAME_HEADER_SIZE)
				{
					return false;
				}

				auto header_data = _packet_buffer.GetWritableDataAs<uint8_t>();

				// Parse HTTP/2 Frame Length (24bit)

				// Parse Length
				_length = (header_data[0] << 16) | (header_data[1] << 8) | header_data[2];

				// Parse Type
				_type = static_cast<Type>(header_data[3]);

				// Parse Flags
				_flags = header_data[4];

				// R: A reserved 1-bit field.  The semantics of this bit are undefined,
				// and the bit MUST remain unset (0x0) when sending and MUST be ignored when receiving.
				header_data[5] &= 0x7F;
				// Parse Stream ID
				_stream_id = (header_data[5] << 24) | (header_data[6] << 16) | (header_data[7] << 8) | header_data[8];

				_state = Http2Frame::ParsingState::HeaderParsed;

				logtt("Debug", "[HTTP/2] Frame Header Parsed. Length: %d, Type: %d, Flags: %d, Stream ID: %d", _length, _type, _flags, _stream_id);

				return true;
			}

			ov::String Http2Frame::ToString() const
			{
				return ov::String::FormatString(
					"<Http2Frame: %p, Type: %d, Flags: %d, Stream ID: %d, Payload Length: %ld>",
					this, GetType(), GetFlags(), GetStreamId(), GetLength());
			}

			std::shared_ptr<ov::Data> Http2Frame::ToData() const
			{
				auto data = std::make_shared<ov::Data>(HTTP2_FRAME_HEADER_SIZE);
				data->SetLength(HTTP2_FRAME_HEADER_SIZE);

				auto payload = GetPayload();
				auto payload_length = payload == nullptr ? 0 : payload->GetLength();

				// Frame Header
				auto header_data = data->GetWritableDataAs<uint8_t>();
				header_data[0] = (payload_length >> 16) & 0xFF;
				header_data[1] = (payload_length >> 8) & 0xFF;
				header_data[2] = payload_length & 0xFF;
				header_data[3] = static_cast<uint8_t>(_type);
				header_data[4] = _flags;
				header_data[5] = (_stream_id >> 24) & 0xFF;
				header_data[6] = (_stream_id >> 16) & 0xFF;
				header_data[7] = (_stream_id >> 8) & 0xFF;
				header_data[8] = _stream_id & 0xFF;

				// Frame Payload
				// It sometimes comes from child class
				if (payload != nullptr)
				{
					data->Append(payload);
				}

				return data;
			}

			void Http2Frame::SetParsingState(ParsingState state) noexcept
			{
				_state = state;
			}

			Http2Frame::ParsingState Http2Frame::GetParsingState() const noexcept
			{
				return _state;
			}

			uint32_t Http2Frame::GetLength() const
			{
				return _length;
			}

			Http2Frame::Type Http2Frame::GetType() const noexcept
			{
				return _type;
			}

			uint8_t Http2Frame::GetFlags() const noexcept
			{
				return _flags;
			}

			bool Http2Frame::IsFlagOn(uint8_t flag) const noexcept
			{
				return (_flags & flag) == flag;
			}

			uint32_t Http2Frame::GetStreamId() const noexcept
			{
				return _stream_id;
			}

			const std::shared_ptr<const ov::Data> Http2Frame::GetPayload() const
			{
				return _payload;
			}

			// Setters
			void Http2Frame::SetType(Type type) noexcept
			{
				_type = type;
			}

			void Http2Frame::SetFlags(uint8_t flags) noexcept
			{
				_flags = flags;
			}

			void Http2Frame::SetFlag(uint8_t flag) noexcept
			{
				_flags |= flag;
			}

			void Http2Frame::SetStreamId(uint32_t stream_id) noexcept
			{
				_stream_id = stream_id;
			}

			void Http2Frame::SetPayload(const std::shared_ptr<const ov::Data> &payload) noexcept
			{
				_payload = payload;
			}

		}  // namespace h2
	}	   // namespace prot
}  // namespace http