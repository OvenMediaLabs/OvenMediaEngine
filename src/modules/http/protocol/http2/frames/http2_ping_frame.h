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
			class Http2PingFrame : public Http2Frame
			{
			public:
				enum class Flags : uint8_t
				{
					None = 0x00,
					Ack = 0x01
				};

				// Make by itself
				Http2PingFrame(uint32_t stream_id)
					: Http2Frame(stream_id)
				{
					SetType(Http2Frame::Type::Ping);
				}

				Http2PingFrame(const std::shared_ptr<Http2Frame> &frame)
					: Http2Frame(frame)
				{
				}

                void SetAck()
                {
                    TURN_ON_HTTP2_FRAME_FLAG(Flags::Ack);
                }

				void SetOpaqueData(const std::shared_ptr<const ov::Data> &data)
				{
					_opaque_data = data;
				}

				bool IsAck() const
				{
					return CHECK_HTTP2_FRAME_FLAG(Flags::Ack);
				}

				const std::shared_ptr<const ov::Data> &GetOpaqueData() const
				{
					return _opaque_data;
				}

				// To String
				ov::String ToString() const override
				{
					ov::String str;
					
					str = Http2Frame::ToString();

					str += "\n";
					str += "[PING Frame]\n";
					
					// Flags - End Header, End Stream, Padded, Priority
					str += ov::String::FormatString("Flags : Ack(%s) \n",
					ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::Ack)).CStr());

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
					payload->Append(GetOpaqueData());

					return payload;
				}

			private:
				bool ParsePayload() override
				{
					if (GetType() != Http2Frame::Type::Ping)
					{
						return false;
					}

                    // PING frames are not associated with any individual stream.  If a PING
                    // frame is received with a stream identifier field value other than
                    // 0x0, the recipient MUST respond with a connection error
                    // (Section 5.4.1) of type PROTOCOL_ERROR.
                    if (GetStreamId() != 0x0)
                    {
                        return false;
                    }

					auto payload = GetPayload();
					if (payload == nullptr)
					{
						return false;
					}

                    if (payload->GetLength() != 8)
                    {
                        return false;
                    }
					
					_opaque_data = payload;

					return true;
				}
				
				std::shared_ptr<const ov::Data> _opaque_data = nullptr;
			};
		}
	}
}