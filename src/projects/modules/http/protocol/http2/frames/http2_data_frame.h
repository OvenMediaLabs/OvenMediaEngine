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
			class Http2DataFrame : public Http2Frame
			{
			public:
				enum class Flags : uint8_t
				{
					None = 0x00,
					EndStream = 0x01,
					Padded = 0x08,
				};

				// Make by itself
				Http2DataFrame(uint32_t stream_id)
					: Http2Frame(stream_id)
				{
					SetType(Http2Frame::Type::Data);
				}

				Http2DataFrame(const std::shared_ptr<Http2Frame> &frame)
					: Http2Frame(frame)
				{
				}

				// Setters
				void SetEndStream()
				{
					TURN_ON_HTTP2_FRAME_FLAG(Flags::EndStream);
				}

				void SetPadLength(uint8_t pad_length)
				{
					_pad_length = pad_length;
					TURN_ON_HTTP2_FRAME_FLAG(Flags::Padded);
				}

				// Set Header Block Fragment
				void SetData(const std::shared_ptr<const ov::Data> &data)
				{
					_data = data;
				}

				// Get Header Block Fragment
				const std::shared_ptr<const ov::Data> &GetData() const
				{
					return _data;
				}

				// To String
				ov::String ToString() const override
				{
					ov::String str;
					
					str = Http2Frame::ToString();

					str += "\n";
					str += "[DATA Frame]\n";
					
					// Header Block Fragment length
					str += ov::String::FormatString("Data Length : %zu\n", _data->GetLength());

					// Flags - End Header, End Stream, Padded, Priority
					str += ov::String::FormatString("Flags : EndStream(%s) Padded(%s) \n",
					ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::EndStream)).CStr(),
					ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::Padded)).CStr());

					// Pad flag and info
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Padded))
					{
						str += ov::String::FormatString("Padded : %d\n", _pad_length);
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

					// Append Header Block Fragment
					payload->Append(GetData());

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
					if (GetType() != Http2Frame::Type::Data)
					{
						return false;
					}

					// DATA frames MUST be associated with a stream.  If a DATA frame is
					// received whose stream identifier field is 0x0, the recipient MUST
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
					size_t data_block_size = payload->GetLength();

					// Get Pad Length if flag is set
					if (CHECK_HTTP2_FRAME_FLAG(Flags::Padded))
					{
						if (data_block_size < 1)
						{
							return false;
						}

						_pad_length = payload_data[payload_offset];
						payload_offset ++;
						data_block_size --;

						// Remove Padding
						data_block_size -= _pad_length;
					}

					// Get Header Block Fragment
					_data = payload->Subdata(payload_offset, data_block_size);

					SetParsingState(ParsingState::Completed);

					return true;
				}
				
				uint8_t	_pad_length = 0;
				std::shared_ptr<const ov::Data> _data = nullptr;
			};
		}
	}
}