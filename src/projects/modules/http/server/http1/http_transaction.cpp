//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_transaction.h"

#include "../http_server_private.h"
#include "../http_connection.h"

namespace http
{
	namespace svr
	{
		namespace h1
		{
			HttpTransaction::HttpTransaction(const std::shared_ptr<HttpConnection> &connection)
				: HttpExchange(connection)
			{
				_request = std::make_shared<Http1Request>(GetConnection()->GetSocket());
				_request->SetConnectionType(ConnectionType::Http11);
				_request->SetTlsData(GetConnection()->GetTlsData());

				_response = std::make_shared<Http1Response>(GetConnection()->GetSocket());
				_response->SetTlsData(GetConnection()->GetTlsData());
				_response->SetHeader("Server", "OvenMediaEngine");
				_response->SetHeader("Content-Type", "text/html");
			}

			std::shared_ptr<HttpRequest> HttpTransaction::GetRequest() const
			{
				return _request;
			}

			std::shared_ptr<HttpResponse> HttpTransaction::GetResponse() const
			{
				return _response;
			}

			ssize_t HttpTransaction::OnRequestPacketReceived(const std::shared_ptr<const ov::Data> &data)
			{
				// Header parsing is complete and data is being received.
				if (_request->GetHeaderParingStatus() == StatusCode::OK)
				{
					auto need_bytes = _request->GetContentLength() - _received_data_size;

					std::shared_ptr<const ov::Data> input_data;
					if (data->GetLength() <= need_bytes)
					{
						input_data = data;
					}
					else
					{
						input_data = data->Subdata(0, need_bytes);
					}

					// Here, payload data is passed to the interceptor rather than stored in the request. The interceptor may or may not store the payload in the request according to each role.
					OnDataReceived(input_data);

					_received_data_size += input_data->GetLength();

					// TODO(getroot) : In the case of chunked transfer, there is no Content-length header, and it is necessary to parse the chunk to determine the end when the length is 0. Currently, chunked-transfer type request is not used, so it is not supported.

					if (_received_data_size >= _request->GetContentLength())
					{
						auto result = OnRequestCompleted();
						switch (result)
						{
							case InterceptorResult::Completed:
								SetStatus(Status::Completed);
								break;
							case InterceptorResult::Moved:
								SetStatus(Status::Moved);
								break;
							case InterceptorResult::NotFound:
								SetStatus(Status::Completed);
								break;
							case InterceptorResult::Error:
							default:
								SetStatus(Status::Error);
								return -1;
						}
					}
					else
					{
						SetStatus(Status::Exchanging);
					}

					return input_data->GetLength();
				}
				// The header has not yet been parsed
				else if (_request->GetHeaderParingStatus() == StatusCode::PartialContent)
				{
					// Put more data to parse header
					auto consumed_bytes = _request->AppendHeaderData(data);
					_received_header_size += consumed_bytes;

					// Check if header parsing is complete
					if (_request->GetHeaderParingStatus() == StatusCode::PartialContent)
					{
						// Need more data to parse header
						return consumed_bytes;
					}
					else if (_request->GetHeaderParingStatus() == StatusCode::OK)
					{
						// Header parsing is done
						if (IsWebSocketUpgradeRequest() == true)
						{
							logtt("Client(%s) is requested uri: [%s] for WebSocket upgrade", GetConnection()->GetSocket()->ToString().CStr(), _request->GetUri().CStr());

							if (AcceptWebSocketUpgrade() == false)
							{
								SetStatus(Status::Error);
								return -1;
							}

							SetStatus(Status::Upgrade);
							return consumed_bytes;
						}
						else
						{
							logtt("Client(%s) is requested uri: [%s]", GetConnection()->GetSocket()->ToString().CStr(), _request->GetUri().CStr());
						}

						SetConnectionPolicyByRequest();

						// Notify to interceptor
						OnRequestPrepared();

						// Check if the request is completed
						// If Content-length is 0, it means that the request is completed.
						if (_request->GetContentLength() == 0)
						{
							auto result = OnRequestCompleted();
							switch (result)
							{
								case InterceptorResult::Completed:
									SetStatus(Status::Completed);
									break;
								case InterceptorResult::Moved:
									SetStatus(Status::Moved);
									break;
								case InterceptorResult::NotFound:
									SetStatus(Status::Completed);
									break;
								case InterceptorResult::Error:
								default:
									SetStatus(Status::Error);
									return -1;
							}
						}
						else
						{
							SetStatus(Status::Exchanging);
						}

						return consumed_bytes;
					}
				}

				// Error
				logte("Invalid parse status: %d", _request->GetHeaderParingStatus());
				return -1;
			}
		}  // namespace h1
	} // namespace svr
}  // namespace http