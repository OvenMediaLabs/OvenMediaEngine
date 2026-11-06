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
				std::shared_ptr<ov::Data> response = std::make_shared<ov::Data>(1024);
				ov::ByteStream stream(response.get());

				// Load once so the status line cannot mix two different status codes
				const auto status_code = GetStatusCode();

				if (_chunked_transfer == false &&
						status_code != StatusCode::NoContent &&
						status_code != StatusCode::NotModified)
				{
					// Calculate the content length
					SetHeader("Content-Length", ov::Converter::ToString(GetResponseDataSize()));
				}

				// RFC7230 - 3.1.2.  Status Line
				// status-line = HTTP-version SP status-code SP reason-phrase CRLF
				// TODO(dimiden): Replace this HTTP version with the version that received from the request
				const auto status_line = ov::String::FormatString("HTTP/1.1 %d %s\r\n", ov::ToUnderlyingType(status_code), StringFromStatusCode(status_code));
				stream.Append(status_line.CStr(), status_line.GetLength());

				// RFC7230 - 3.2.  Header Fields
				VisitResponseHeaderList([&stream](const auto &header_list) {
					for (const auto &[key, values] : header_list)
					{
						for (const auto &value : values)
						{
							stream.Append(key.CStr(), key.GetLength());
							stream.Append(": ", 2);
							stream.Append(value.CStr(), value.GetLength());
							stream.Append("\r\n", 2);
						}
					}
				});

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
				logtt("Trying to send datas...");

				uint32_t sent_bytes = 0;
				const bool all_sent = VisitResponseDataList([&](const auto &data_list) -> bool {
					for (const auto &data : data_list)
					{
						const bool ok = _chunked_transfer ? SendChunkedData(data) : Send(data);
						if (!ok)
						{
							logte("Could not send %sdata : %zu bytes", _chunked_transfer ? "chunked " : "", data->GetLength());
							return false;
						}
						sent_bytes += data->GetLength();
					}
					return true;
				});

				if (all_sent == false)
				{
					return -1;
				}

				ResetResponseData();

				logtt("All datas are sent...");

				return sent_bytes;
			}
		} // namespace h1
	} // namespace svr
} // namespace http