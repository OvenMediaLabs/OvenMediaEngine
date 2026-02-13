//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovlibrary/byte_io.h>
#include "../http2_frame.h"

namespace http
{
	namespace prot
	{
		namespace h2
		{
			class Http2HeadersFrame : public Http2Frame
			{
			public:
				enum class Flags : uint8_t
				{
					None = 0x00,
					EndHeaders = 0x04,
					EndStream = 0x01,
					Padded = 0x08,
					Priority = 0x20,
				};

				// Make by itself
				Http2HeadersFrame(uint32_t stream_id)
					: Http2Frame(stream_id)
				{
					SetType(Http2Frame::Type::Headers);
				}

				Http2HeadersFrame(const std::shared_ptr<Http2Frame> &frame)
					: Http2Frame(frame)
				{
				}

				// Setters
				void SetEndHeaders()
				{
					TURN_ON_HTTP2_FRAME_FLAG(Flags::EndHeaders);
				}

				void SetEndStream()
				{
					TURN_ON_HTTP2_FRAME_FLAG(Flags::EndStream);
				}

				void SetPadLength(uint8_t pad_length)
				{
					_pad_length = pad_length;
					SetFlag(static_cast<uint8_t>(Flags::Padded));
				}

				void SetPriority(bool is_exclusive, uint32_t stream_dependency, uint8_t weight)
				{
					_is_exclusive = is_exclusive;
					_stream_dependency = stream_dependency;
					_weight = weight;
					TURN_ON_HTTP2_FRAME_FLAG(Flags::Priority);
				}

				// Set Header Block Fragment
				void SetHeaderBlockFragment(const std::shared_ptr<const ov::Data> &data)
				{
					_header_block_fragment = data;
				}

				// Get Header Block Fragment
				const std::shared_ptr<const ov::Data> &GetHeaderBlockFragment() const
				{
					return _header_block_fragment;
				}

				// To String
				ov::String ToString() const override
				{
					ov::String str;
					
					str = Http2Frame::ToString();

					str += "\n";
					str += "[Headers Frame]\n";
					
					// Header Block Fragment length
					str += ov::String::FormatString("Header Block Fragment Length : %zu\n", _header_block_fragment->GetLength());

					// Flags - End Header, End Stream, Padded, Priority
					str += ov::String::FormatString("Flags : EndHeader(%s) EndStream(%s) Padded(%s) Priority(%s)\n",
					ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::EndHeaders)).CStr(), 
					ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::EndStream)).CStr(),
					ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::Padded)).CStr(),
					ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::Priority)).CStr());

					// Pad flag and info
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Padded))
					{
						str += ov::String::FormatString("Padded : %d\n", _pad_length);
					}

					// Priority flag
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Priority))
					{
						str += ov::String::FormatString("Exclusive : %s\n", _is_exclusive ? "true" : "false");
						str += ov::String::FormatString("Stream Dependency : %d\n", _stream_dependency);
						str += ov::String::FormatString("Weight : %d\n", _weight);
					}

					return str;
				}

				// To data
				const std::shared_ptr<const ov::Data> GetPayload() const override
				{
					if (Http2Frame::GetPayload() != nullptr)
					{
						return Http2Frame::GetPayload();
					}

					auto payload = std::make_shared<ov::Data>();
					ov::ByteStream stream(payload.get()); 

					// Set Pad Length if needed
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Padded))
					{
						stream.Write8(_pad_length);
					}

					// Set Priority if needed
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Priority))
					{
						uint32_t stream_dependency_with_flag = _stream_dependency;
						stream_dependency_with_flag |= (_is_exclusive ? 0x80000000 : 0x00000000);

						stream.WriteBE32(stream_dependency_with_flag);
						stream.Write8(_weight);
					}

					// Append Header Block Fragment
					payload->Append(GetHeaderBlockFragment());

					// Append Padding if needed
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Padded))
					{
						payload->SetLength(payload->GetLength() + _pad_length);
					}

					return payload;
				}

			private:
				bool ParsePayload() override
				{
					if (GetType() != Type::Headers)
					{
						return false;
					}

					// HEADERS frames MUST be associated with a stream.  If a HEADERS frame
					// is received whose stream identifier field is 0x0, the recipient MUST
					// respond with a connection error (Section 5.4.1) of type
					// PROTOCOL_ERROR.
					if (GetStreamId() == 0)
					{
						return false;
					}

					auto payload = GetPayload();
					if (payload == nullptr)
					{
						return false;
					}
					
					// Parse each parameter in the payload
					auto payload_data = payload->GetDataAs<uint8_t>();
					auto payload_offset = 0;
					size_t header_block_size = payload->GetLength();

					// Get Pad Length if flag is set
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Padded))
					{
						if (header_block_size < 1)
						{
							return false;
						}

						_pad_length = payload_data[payload_offset];
						payload_offset ++;
						header_block_size --;

						// Remove Padding
						if (header_block_size < _pad_length)
						{
							return false;
						}

						header_block_size -= _pad_length;
					}

					// Get Priority if flag is set
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Priority))
					{
						if (header_block_size < 5)
						{
							return false;
						}

						// Get Exclusive Flag
						_is_exclusive = payload_data[payload_offset] & 0x80;
						
						// Get Stream Dependency from payload_data (31 bits)
						_stream_dependency = ByteReader<uint32_t>::ReadBigEndian(payload_data + payload_offset);
						// Remove E flag
						_stream_dependency &= 0x7FFFFFFF;

						payload_offset += 4;
						header_block_size -= 4;
						
						// Get Weight
						_weight = payload_data[payload_offset];
						payload_offset ++;
						header_block_size --;
					}

					// Get Header Block Fragment
					if (header_block_size <= 0)
					{
						return false;
					}
					
					_header_block_fragment = payload->Subdata(payload_offset, header_block_size);

					return true;
				}
				
				uint8_t	_pad_length = 0;
				bool	_is_exclusive = false;
				uint32_t _stream_dependency = 0;
				uint8_t	_weight = 0;
				std::shared_ptr<const ov::Data> _header_block_fragment = nullptr;
			};
		}
	}
}