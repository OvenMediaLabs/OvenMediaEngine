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
namespace http
{
	namespace prot
	{
		namespace h2
		{
			// https://datatracker.ietf.org/doc/html/rfc7540#section-3.5

			// In HTTP/2, each endpoint is required to send a connection preface as
			// a final confirmation of the protocol in use and to establish the
			// initial settings for the HTTP/2 connection.  The client and server
			// each send a different connection preface.
			// The client connection preface starts with a sequence of 24 octets,
			// which in hex notation is:
			// 0x505249202a20485454502f322e300d0a0d0a534d0d0a0d0a
			// That is, the connection preface starts with the string "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n").

			class Http2Preface
			{
			public:
				static constexpr const char *PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
				static constexpr size_t PREFACE_LENGTH = 24;

				ssize_t AppendData(const std::shared_ptr<const ov::Data> &data)
				{
					// Copies data into the buffer as needed to parse the preface.
					auto needed_size = PREFACE_LENGTH - _buffer.GetLength();
					if (data->GetLength() < needed_size)
					{
						_buffer.Append(data);
						return data->GetLength();
					}
					else
					{
						_buffer.Append(data->GetData(), needed_size);
					}

					if (_buffer.GetLength() == PREFACE_LENGTH)
					{
						if (memcmp(_buffer.GetData(), PREFACE, PREFACE_LENGTH) == 0)
						{
							logt("Debug", "HTTP/2.0 preface received");
							_confirmed = true;
						}
						else
						{
							return -1;
						}
					}
					else
					{
						return -1;
					}

					// Returns the number of bytes consumed.
					return needed_size;
				}

				bool IsConfirmed() const
				{
					return _confirmed;
				}

			private:
				bool _confirmed = false;
				ov::Data _buffer;
			};
		}  // namespace h2
	} // namespace prot
}  // namespace http