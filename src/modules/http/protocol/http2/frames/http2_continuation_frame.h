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
			class Http2ContinuationFrame : public Http2Frame
			{
			public:
				enum class Flags : uint8_t
				{
					None = 0x00,
					EndHeaders = 0x04
				};

				// Make by itself
				Http2ContinuationFrame(uint32_t stream_id)
					: Http2Frame(stream_id)
				{
					SetType(Http2Frame::Type::Headers);
				}

				Http2ContinuationFrame(const std::shared_ptr<Http2Frame> &frame)
					: Http2Frame(frame)
				{
				}

				// Setters
				void SetEndHeaders()
				{
					TURN_ON_HTTP2_FRAME_FLAG(Flags::EndHeaders);
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
					str += "[Continuation Frame]\n";
					
					// Header Block Fragment length
					str += ov::String::FormatString("Header Block Fragment Length : %zu\n", _header_block_fragment->GetLength());

					// Flags - End Header
					str += ov::String::FormatString("Flags : EndHeader(%s)\n",
					ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::EndHeaders)).CStr());

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

					// Append Header Block Fragment
					payload->Append(GetHeaderBlockFragment());

					return payload;
				}

			private:
				bool ParsePayload() override
				{
					if (GetType() != Type::Continuation)
					{
						return false;
					}

					// CONTINUATION frames MUST be associated with a stream.  If a
					// CONTINUATION frame is received whose stream identifier field is 0x0,
					// the recipient MUST respond with a connection error (Section 5.4.1) of
					// type PROTOCOL_ERROR.
					if (GetStreamId() == 0)
					{
						return false;
					}

					auto payload = GetPayload();
					if (payload == nullptr)
					{
						return false;
					}

					// Get Header Block Fragment
					_header_block_fragment = payload;

					SetParsingState(ParsingState::Completed);

					return true;
				}

				std::shared_ptr<const ov::Data> _header_block_fragment = nullptr;
			};
		}
	}
}