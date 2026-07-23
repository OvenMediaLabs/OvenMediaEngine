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
			class Http2PriorityFrame : public Http2Frame
			{
			public:
				// Make by itself
				Http2PriorityFrame(uint32_t stream_id)
					: Http2Frame(stream_id)
				{
					SetType(Http2Frame::Type::Priority);
				}

				Http2PriorityFrame(const std::shared_ptr<Http2Frame> &frame)
					: Http2Frame(frame)
				{
				}

				// Setters
				void SetPriority(bool is_exclusive, uint32_t stream_dependency, uint8_t weight)
				{
					_is_exclusive = is_exclusive;
					_stream_dependency = stream_dependency;
					_weight = weight;
				}

				// To String
				ov::String ToString() const override
				{
					ov::String str;
					
					str = Http2Frame::ToString();

					str += "\n";
					str += "[Priority Frame]\n";

					str += ov::String::FormatString("Exclusive : %s\n", _is_exclusive ? "true" : "false");
					str += ov::String::FormatString("Stream Dependency : %d\n", _stream_dependency);
					str += ov::String::FormatString("Weight : %d\n", _weight);

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

					uint32_t stream_dependency_with_flag = _stream_dependency;
				    stream_dependency_with_flag |= (_is_exclusive ? 0x80000000 : 0x00000000);

					stream.WriteBE32(stream_dependency_with_flag);
					stream.Write8(_weight);

					return payload;
				}

			private:
				bool ParsePayload() override
				{
					if (GetType() != Type::Priority)
					{
						return false;
					}

					// The PRIORITY frame always identifies a stream.  If a PRIORITY frame
					// is received with a stream identifier of 0x0, the recipient MUST
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

                    if (payload->GetLength() < 5)
                    {
                        return false;
                    }
					
					// Parse each parameter in the payload
					auto payload_data = payload->GetDataAs<uint8_t>();
					auto payload_offset = 0;

                    // Get Exclusive Flag
                    _is_exclusive = payload_data[payload_offset] & 0x80;
                    
                    // Get Stream Dependency from payload_data (31 bits)
                    _stream_dependency = ByteReader<uint32_t>::ReadBigEndian(payload_data + payload_offset);
                    // Remove E flag
                    _stream_dependency &= 0x7FFFFFFF;

                    payload_offset += 4;
                    
                    // Get Weight
                    _weight = payload_data[payload_offset];

					return true;
				}
				
				bool	_is_exclusive = false;
				uint32_t _stream_dependency = 0;
				uint8_t	_weight = 0;
			};
		}
	}
}