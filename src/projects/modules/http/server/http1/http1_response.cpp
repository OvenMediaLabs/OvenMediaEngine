//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http1_response.h"
#include "../http_server_private.h"

namespace http
{
	namespace svr
	{
		namespace h1
		{
			Http1Response::Http1Response(const std::shared_ptr<ov::ClientSocket> &client_socket)
				: HttpResponse(client_socket)
			{
			}

			bool Http1Response::SendChunkedData(const void *data, size_t length)
			{
				return SendChunkedData(std::make_shared<ov::Data>(data, length));
			}

			bool Http1Response::SendChunkedData(const std::shared_ptr<const ov::Data> &data)
			{
				if ((data == nullptr) || data->IsEmpty())
				{
					// Send a empty chunk
					return Send("0\r\n\r\n", 5);
				}

				bool result =
					// Send the chunk header
					Send(ov::String::FormatString("%" PRIx64 "\r\n", data->GetLength()).ToData(false)) &&
					// Send the chunk payload
					Send(data) &&
					// Send a last data of chunk
					Send("\r\n", 2);

				return result;
			}

			void Http1Response::SetChunkedTransfer()
			{
				SetHeader("Transfer-Encoding", "chunked");
				_chunked_transfer = true;
			}

			bool Http1Response::IsChunkedTransfer() const
			{
				return _chunked_transfer;
			}

			int32_t Http1Response::SendHeader()
			{
				std::shared_ptr<ov::Data> response = std::make_shared<ov::Data>(65535);
				ov::ByteStream stream(response.get());

				if (_chunked_transfer == false && 
						GetStatusCode() != StatusCode::NoContent && 
						GetStatusCode() != StatusCode::NotModified)
				{
					// Calculate the content length
					SetHeader("Content-Length", ov::Converter::ToString(GetResponseDataSize()));
				}

				// RFC7230 - 3.1.2.  Status Line
				// status-line = HTTP-version SP status-code SP reason-phrase CRLF
				// TODO(dimiden): Replace this HTTP version with the version that received from the request
				stream.Append(ov::String::FormatString("HTTP/1.1 %d %s\r\n", ov::ToUnderlyingType(GetStatusCode()), GetReason().CStr()).ToData(false));

				// RFC7230 - 3.2.  Header Fields
				for (const auto &pair : GetResponseHeaderList())
				{
					auto key_data = pair.first.ToData(false);
					const std::vector<ov::String> &value_list = pair.second;

					for (const auto &value : value_list)
					{
						auto value_data = value.ToData(false);

						stream.Append(key_data);
						stream.Append(": ", 2);
						stream.Append(value_data);
						stream.Append("\r\n", 2);
					}
				}

				stream.Append("\r\n", 2);

				if (Send(response))
				{
					logtt("Header is sent:\n%s", response->Dump(response->GetLength()).CStr());
					return response->GetLength();
				}

				logte("Could not send header");
				return -1;
			}

			int32_t Http1Response::SendPayload()
			{
				bool sent = true;

				logtt("Trying to send datas...");

				uint32_t sent_bytes = 0;
				for (const auto &data : GetResponseDataList())
				{
					if (_chunked_transfer)
					{
						sent &= SendChunkedData(data);
						if (sent == true)
						{
							sent_bytes += data->GetLength();
						}
						else
						{
							logte("Could not send chunked data : %zu bytes", data->GetLength());
							return -1;
						}
					}
					else
					{
						sent &= Send(data);
						if (sent == true)
						{
							sent_bytes += data->GetLength();
						}
						else
						{
							logte("Could not send data : %zu bytes", data->GetLength());
							return -1;
						}
					}
				}

				ResetResponseData();

				logtt("All datas are sent...");

				return sent_bytes;
			}
		} // namespace h1
	} // namespace svr
} // namespace http