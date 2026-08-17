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
            // Not yet implemented
			class Http2PushPromiseFrame : public Http2Frame
			{
			public:
				// Make by itself
				Http2PushPromiseFrame()
					: Http2Frame(0)
				{
					SetType(Http2Frame::Type::PushPromise);
				}

				Http2PushPromiseFrame(const std::shared_ptr<Http2Frame> &frame)
					: Http2Frame(frame)
				{
				}

				// To String
				ov::String ToString() const override
				{
					ov::String str;
					
					str = Http2Frame::ToString();

					str += "\n";
					str += "[PUSH_PROMISE Frame]\n";

					str += ov::String::FormatString("Not yet implemented\n");

					return str;
				}

				// To data
				const std::shared_ptr<const ov::Data> GetPayload() const override
				{
					if (Http2Frame::GetPayload() != nullptr)
					{
						return Http2Frame::GetPayload();
					}

					return nullptr;
				}

			private:
				bool ParsePayload() override
				{
					if (GetType() != Http2Frame::Type::PushPromise)
					{
						return false;
					}

					SetParsingState(ParsingState::Error);

					return true;
				}
			};
		}
	}
}