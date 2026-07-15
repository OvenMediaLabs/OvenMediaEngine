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
			class Http2WindowUpdateFrame : public Http2Frame
			{
			public:
				// Make by itself
				Http2WindowUpdateFrame(uint32_t stream_id)
					: Http2Frame(stream_id)
				{
					SetType(Http2Frame::Type::WindowUpdate);
					// Unused flags
					SetFlags(0);
				}

				Http2WindowUpdateFrame(const std::shared_ptr<Http2Frame> &frame)
					: Http2Frame(frame)
				{
				}

				// Get Window Size Increment
				uint32_t GetWindowSizeIncrement() const
				{
					return _window_size_increment;
				}

				// Set Window Size Increment
				bool SetWindowSizeIncrement(uint32_t window_size_increment)
				{
					// can't be 0  and can't be larger than 2^31-1 (RFC 7540 Section 6.9)
					OV_ASSERT2(window_size_increment > 0 && window_size_increment <= 0x7FFFFFFF);
					
					if (window_size_increment > 0 && window_size_increment <= 0x7FFFFFFF)
					{
						_window_size_increment = window_size_increment;
						return true;
					}

					return false;
				}

				// To String
				ov::String ToString() const override
				{
					ov::String str;
					
					str = Http2Frame::ToString();

					str += "\n";
					str += "[WindowUpdate Frame]\n";
					str += ov::String::FormatString("Window size increment: %u\n", GetWindowSizeIncrement());
				
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
					stream.WriteBE32(_window_size_increment);

					return payload;
				}

			private:
				bool ParsePayload() override
				{
					if (GetType() != Http2Frame::Type::WindowUpdate)
					{
						return false;
					}

					// The WINDOW_UPDATE frame can be specific to a stream or to the entire
					// connection.  In the former case, the frame's stream identifier
					// indicates the affected stream; in the latter, the value "0" indicates
					// that the entire connection is the subject of the frame.

					// WindowUpdate frame must have a payload of at least 4 bytes
					auto payload = GetPayload();
					if (payload == nullptr)
					{
						SetParsingState(ParsingState::Error);
						return false;
					}
					
					// Parse each parameter in the payload
					auto payload_data = payload->GetDataAs<uint8_t>();
					auto payload_size = payload->GetLength();

					if (payload_size != 4)
					{
						SetParsingState(ParsingState::Error);
						return false;
					}

					_window_size_increment = ByteReader<uint32_t>::ReadBigEndian(payload_data);
					
					SetParsingState(ParsingState::Completed);

					return true;
				}

				uint32_t _window_size_increment = 983041; // Default value
			};
		}
	}
}