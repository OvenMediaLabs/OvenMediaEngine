//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_client.h"

#include "./http_client_private.h"

#define HTTP_CLIENT_READ_BUFFER_SIZE (64 * 1024)
#define HTTP_CLIENT_MAX_CHUNK_HEADER_LENGTH (32)
#define HTTP_CLIENT_NEW_LINE "\r\n"
#define HTTP_CLIENT_NEW_LINE_LENGTH (OV_COUNTOF(HTTP_CLIENT_NEW_LINE) - 1)

namespace http
{
	namespace clnt
	{
		HttpClient::HttpClient()
			: _socket_pool(ov::SocketPool::GetTcpPool())
		{
			// Default request headers
			_request_header["User-Agent"] = "OvenMediaEngine";
			_request_header["Accept"] = "*/*";
		}

		HttpClient::HttpClient(const std::shared_ptr<ov::SocketPool> &socket_pool)
			: _socket_pool(socket_pool)
		{
			OV_ASSERT2(socket_pool != nullptr);
		}

		HttpClient::~HttpClient()
		{
		}

		void HttpClient::SetBlockingMode(ov::BlockingMode mode)
		{
			_blocking_mode = mode;
		}

		ov::BlockingMode HttpClient::GetBlockingMode() const
		{
			return _blocking_mode;
		}

		void HttpClient::SetConnectionTimeout(int timeout_in_msec)
		{
			_connection_timeout_msec = timeout_in_msec;
		}

		int HttpClient::GetConnectionTimeout() const
		{
			return _connection_timeout_msec;
		}

		void HttpClient::SetRecvTimeout(int timeout_msec)
		{
			_recv_timeout_msec = timeout_msec;
		}

		int HttpClient::GetRecvTimeout() const
		{
			return _recv_timeout_msec;
		}

		void HttpClient::SetMethod(http::Method method)
		{
			_method = method;
		}

		http::Method HttpClient::GetMethod() const
		{
			return _method;
		}

		void HttpClient::SetRequestHeader(const ov::String &key, const ov::String &value)
		{
			std::lock_guard lock_guard(_request_mutex);

			if (_requested)
			{
				logte("Could not set header after request: %s", key.CStr());
				return;
			}

			_request_header[key] = value;
		}

		ov::String HttpClient::GetRequestHeader(const ov::String &key)
		{
			auto iterator = _request_header.find(key);

			if (iterator == _request_header.end())
			{
				return "";
			}

			return iterator->second;
		}

		const std::unordered_map<ov::String, ov::String, ov::CaseInsensitiveHash, ov::CaseInsensitiveEqual> &HttpClient::GetRequestHeaders() const
		{
			return _request_header;
		}

		std::unordered_map<ov::String, ov::String, ov::CaseInsensitiveHash, ov::CaseInsensitiveEqual> &HttpClient::GetRequestHeaders()
		{
			return _request_header;
		}

		void HttpClient::SetRequestBody(const std::shared_ptr<const ov::Data> &body)
		{
			_request_body = body->Clone();
		}

		void HttpClient::OnConnected(const std::shared_ptr<const ov::SocketError> &error)
		{
			if (error == nullptr)
			{
				OnReadable();
				return;
			}

			auto detail_error = ov::Error::CreateError("HTTP", error->GetCode(), "Could not connect to %s (in callback): %s", _url.CStr(), error->GetMessage().CStr());

			HandleError(detail_error);
		}

		void HttpClient::OnReadable()
		{
			auto error = TryTlsConnect();

			if (error != nullptr)
			{
				if (error->GetCode() == SSL_ERROR_WANT_READ)
				{
					// Need more data
				}
				else
				{
					logtt("Could not connect TLS: %s", error->What());
					HandleError(error);
				}

				return;
			}

			SendRequestIfNeeded();
			RecvResponse();
		}

		void HttpClient::OnClosed()
		{
			// Ignore
		}

		ssize_t HttpClient::OnTlsReadData(void *data, int64_t length)
		{
			auto socket = _socket;

			if (socket != nullptr)
			{
				size_t received_length;
				auto error = socket->Recv(data, length, &received_length);

				if (error == nullptr)
				{
					return received_length;
				}
			}

			return -1;
		}

		ssize_t HttpClient::OnTlsWriteData(const void *data, int64_t length)
		{
			auto socket = _socket;

			if (socket != nullptr)
			{
				if (socket->Send(data, length))
				{
					return length;
				}
			}

			return -1;
		}

		std::shared_ptr<const ov::Error> HttpClient::PrepareForRequest(const ov::String &url, ov::SocketAddress *address)
		{
			if (_requested)
			{
				return ov::Error::CreateError("HTTP", "It has already been requested before: %s", _url.CStr());
			}

			if (url.IsEmpty())
			{
				return ov::Error::CreateError("HTTP", "URL must not be empty");
			}

			if (_socket_pool == nullptr)
			{
				return ov::Error::CreateError("HTTP", "Socket pool cannot be nullptr");
			}

			auto parsed_url = ov::Url::Parse(url);

			if (parsed_url == nullptr)
			{
				return ov::Error::CreateError("HTTP", "Could not parse URL: %s", url.CStr());
			}

			bool is_https = false;
			auto scheme = parsed_url->Scheme().UpperCaseString();

			if (scheme == "HTTPS")
			{
				is_https = true;
			}
			else if (scheme == "HTTP")
			{
				is_https = false;
			}
			else
			{
				return ov::Error::CreateError("HTTP", "Unknown scheme: %s, URL: %s", parsed_url->Scheme().CStr(), url.CStr());
			}

			auto port = parsed_url->Port();
			bool use_default_port = (port == 0);
			if (use_default_port)
			{
				port = is_https ? 443 : 80;
				parsed_url->SetPort(port);
			}

			auto host_port_string = ov::String::FormatString("%s:%d", parsed_url->Host().CStr(), port);
			auto socket_address = ov::SocketAddress::CreateAndGetFirst(host_port_string);

			if (socket_address.IsValid() == false)
			{
				return ov::Error::CreateError("HTTP", "Invalid address: %s, URL: %s", host_port_string.CStr(), url.CStr());
			}

			_socket = _socket_pool->AllocSocket(socket_address.GetFamily());

			if (_socket == nullptr)
			{
				return ov::Error::CreateError("HTTP", "Could not create a socket");
			}

			if (((_blocking_mode == ov::BlockingMode::Blocking) ? _socket->MakeBlocking() : _socket->MakeNonBlocking(GetSharedPtr())) == false)
			{
				return ov::Error::CreateError("HTTP", "Could not set blocking mode");
			}

			if (is_https)
			{
				std::shared_ptr<const ov::Error> error;
				_tls_data = std::make_shared<ov::TlsClientData>(ov::TlsContext::CreateClientContext(&error), (_blocking_mode == ov::BlockingMode::NonBlocking));

				if (_tls_data == nullptr)
				{
					return error;
				}

				_tls_data->SetIoCallback(GetSharedPtrAs<ov::TlsClientDataIoCallback>());

				const auto &hostname = socket_address.GetHostname();

				if (hostname.IsEmpty() == false)
				{
					_tls_data->SetTlsHostName(socket_address.GetHostname());
				}
			}

			if (address != nullptr)
			{
				*address = socket_address;
			}

			_url = url;
			_parsed_url = parsed_url;

			_request_header["Host"] =
				use_default_port
					? ov::String::FormatString("%s", _parsed_url->Host().CStr())
					: ov::String::FormatString("%s:%d", _parsed_url->Host().CStr(), _parsed_url->Port());

			return nullptr;
		}

		void HttpClient::SendRequestIfNeeded()
		{
			if (_requested)
			{
				return;
			}

			_requested = true;

			auto path = _parsed_url->Path();

			if (path.IsEmpty())
			{
				path = "/";
			}

			auto query_string = _parsed_url->Query();
			if (query_string.IsEmpty() == false)
			{
				path.AppendFormat("?%s", query_string.CStr());
			}

			ov::String request_header;

			// Make HTTP 1.1 request header
			logtt("Request resource: %s", path.CStr());

			// Pick a first method in _method
			request_header.AppendFormat("%s %s HTTP/1.1" HTTP_CLIENT_NEW_LINE, http::StringFromMethod(_method, false).CStr(), path.CStr());

			if (_request_body != nullptr)
			{
				_request_header["Content-Length"] = ov::Converter::ToString(_request_body->GetLength());
			}

			logtt("Request headers: total %zu item(s):", _request_header.size());

			for (auto header : _request_header)
			{
				request_header.AppendFormat("%s: %s" HTTP_CLIENT_NEW_LINE, header.first.CStr(), header.second.CStr());
				logtt("  >> %s: %s", header.first.CStr(), header.second.CStr());
			}

			request_header.Append(HTTP_CLIENT_NEW_LINE);

			SendData(request_header.ToData(false));
			SendData(_request_body);
		}

		std::shared_ptr<const ov::OpensslError> HttpClient::TryTlsConnect()
		{
			auto tls_data = _tls_data;

			if ((tls_data == nullptr) || (tls_data->GetState() == ov::TlsClientData::State::Connected))
			{
				// Nothing to do - the request isn't HTTPs request or already connected
				return nullptr;
			}

			return tls_data->Connect();
		}

		void HttpClient::Request(const ov::String &url, ResponseHandler response_handler)
		{
			std::lock_guard lock_guard(_request_mutex);

			ov::SocketAddress address;

			_response_handler = response_handler;

			auto error = PrepareForRequest(url, &address);

			if (error == nullptr)
			{
				OV_ASSERT2(_url.IsEmpty() == false);
				OV_ASSERT2(_parsed_url != nullptr);

				logtt("Request an URL: %s (address: %s)...", url.CStr(), address.ToString(false).CStr());

				// Convert milliseconds to timeval
				_socket->SetRecvTimeout(
					{.tv_sec = _recv_timeout_msec / 1000,
					 .tv_usec = _recv_timeout_msec % 1000});

				error = _socket->Connect(address, _connection_timeout_msec);

				if (error == nullptr)
				{
					if (_socket->GetBlockingMode() == ov::BlockingMode::NonBlocking)
					{
						// Data will be downloaded in OnReadable()
						return;
					}

					OnConnected(nullptr);
					return;
				}

				error = ov::Error::CreateError("HTTP", error->GetCode(), "Could not connect to %s: %s", url.CStr(), error->GetMessage().CStr());
			}

			HandleError(error);
		}

		ov::String HttpClient::GetResponseHeader(const ov::String &key)
		{
			return _parser.GetHeader(key).value_or("");
		}

		const std::unordered_map<ov::String, ov::String, ov::CaseInsensitiveHash, ov::CaseInsensitiveEqual> &HttpClient::GetResponseHeaders() const
		{
			return _parser.GetHeaders();
		}

		void HttpClient::RecvResponse()
		{
			auto socket = _socket;
			auto tls_data = _tls_data;
			std::shared_ptr<ov::Data> data;
			std::shared_ptr<const ov::Data> process_data;
			std::shared_ptr<const ov::Error> error;
			bool need_to_callback = false;

			if (tls_data == nullptr)
			{
				data = std::make_shared<ov::Data>(HTTP_CLIENT_READ_BUFFER_SIZE);
			}

			while (true)
			{
				if (tls_data != nullptr)
				{
					if (tls_data->Decrypt(&process_data) == false)
					{
						error = ov::Error::CreateError("HTTP", "Could not decrypt data");
					}
					else if (process_data->GetLength() == 0)
					{
						if (_blocking_mode == ov::BlockingMode::NonBlocking)
						{
							// Need more data
							return;
						}

						// Connection has been closed
						error = ov::Error::CreateError("HTTP", "Connection closed by the server");
					}
				}
				else
				{
					error = socket->Recv(data);

					if (error == nullptr)
					{
						if ((data->GetLength() == 0) && (_blocking_mode == ov::BlockingMode::NonBlocking))
						{
							// Read data next time in Nonblocking mode
							return;
						}

						process_data = data;
					}
					else
					{
						// An error occurred
					}
				}

				if (error == nullptr)
				{
					error = ProcessData(process_data);
				}

				if (error != nullptr)
				{
					switch (socket->GetState())
					{
						case ov::SocketState::Closed:
							[[fallthrough]];
						case ov::SocketState::Disconnected:
							// Ignore the error
							need_to_callback = true;
							error = nullptr;
							break;

						default:
							// Free the allocated data
							_response_body = nullptr;
							break;
					}

					break;
				}

				if (
					((_response_body != nullptr) &&
					 ((_parser.HasContentLength() && (_response_body->GetLength() >= _parser.GetContentLength())) ||
					  (_chunk_parse_status == ChunkParseStatus::Completed))) ||
					need_to_callback)
				{
					// All data received
					break;
				}
			}

			auto response_handler = _response_handler;

			if (response_handler != nullptr)
			{
				response_handler(_parser.GetStatusCode(), _response_body, error);
			}

			CleanupVariables();
		}

		std::shared_ptr<const ov::Error> HttpClient::ProcessChunk(const std::shared_ptr<const ov::Data> &data, size_t *processed_bytes)
		{
			auto remained = data->GetLength();
			auto sub_data = data->GetDataAs<char>();

			*processed_bytes = 0L;

			while (remained > 0L)
			{
				switch (_chunk_parse_status)
				{
					case ChunkParseStatus::None:
						OV_ASSERT2(false);
						return ov::Error::CreateError("HTTP", "Internal server error: Invalid ChunkParseStatus");

					case ChunkParseStatus::Header: {
						// ov::String is binary-safe
						ov::String temp_header(sub_data, remained);

						auto position = temp_header.IndexOf(HTTP_CLIENT_NEW_LINE);
						if (position >= 0)
						{
							// Found a separator
							_chunk_parse_status = ChunkParseStatus::Body;

							_chunk_header.Append(sub_data, position);
							sub_data += position + HTTP_CLIENT_NEW_LINE_LENGTH;
							remained -= position + HTTP_CLIENT_NEW_LINE_LENGTH;
							*processed_bytes += position + HTTP_CLIENT_NEW_LINE_LENGTH;

							_chunk_length = ::strtoll(_chunk_header, nullptr, 16);
							_chunk_header.Clear();

							if (_chunk_length == 0)
							{
								_chunk_parse_status = ChunkParseStatus::CarriageReturnEnd;
							}
						}
						else
						{
							_chunk_header.Append(sub_data, remained);
							remained = 0;
							*processed_bytes += remained;
						}

						if (_chunk_header.GetLength() > HTTP_CLIENT_MAX_CHUNK_HEADER_LENGTH)
						{
							return ov::Error::CreateError("HTTP", "Chunk header is too long");
						}

						break;
					}

					case ChunkParseStatus::Body: {
						auto bytes_to_copy = std::min(_chunk_length, remained);

						_response_body->Append(sub_data, bytes_to_copy);

						sub_data += bytes_to_copy;
						remained -= bytes_to_copy;
						_chunk_length -= bytes_to_copy;
						*processed_bytes += bytes_to_copy;

						if (_chunk_length == 0)
						{
							_chunk_parse_status = ChunkParseStatus::CarriageReturn;
						}

						break;
					}

					case ChunkParseStatus::CarriageReturn:
						if ((*sub_data) == '\r')
						{
							sub_data++;
							remained--;
							*processed_bytes += 1;
							_chunk_parse_status = ChunkParseStatus::LineFeed;
						}
						else
						{
							return ov::Error::CreateError("HTTP", "Invalid chunk header (line feed not found)");
						}

						break;

					case ChunkParseStatus::LineFeed:
						if ((*sub_data) == '\n')
						{
							sub_data++;
							remained--;
							*processed_bytes += 1;
							_chunk_parse_status = ChunkParseStatus::Header;
						}
						else
						{
							return ov::Error::CreateError("HTTP", "Invalid chunk header (new line not found)");
						}

						break;

					case ChunkParseStatus::CarriageReturnEnd:
						if ((*sub_data) == '\r')
						{
							sub_data++;
							remained--;
							*processed_bytes += 1;
							_chunk_parse_status = ChunkParseStatus::LineFeedEnd;
						}
						else
						{
							return ov::Error::CreateError("HTTP", "Invalid chunk header (line feed not found)");
						}

						break;

					case ChunkParseStatus::LineFeedEnd:
						if ((*sub_data) == '\n')
						{
							sub_data++;
							remained--;
							*processed_bytes += 1;
							_chunk_parse_status = ChunkParseStatus::Completed;
						}
						else
						{
							return ov::Error::CreateError("HTTP", "Invalid chunk header (new line not found)");
						}

						break;

					case ChunkParseStatus::Completed:
						return nullptr;
				}
			}

			return nullptr;
		}

		std::shared_ptr<const ov::Error> HttpClient::ProcessData(const std::shared_ptr<const ov::Data> &data)
		{
			auto remained = data->GetLength();
			auto sub_data = data;

			while (remained > 0)
			{
				switch (_parser.GetStatus())
				{
					case StatusCode::OK: {
						// Parsing is completed
						size_t processed_bytes;

						if (_is_chunked_transfer)
						{
							auto error = ProcessChunk(sub_data, &processed_bytes);

							if (error != nullptr)
							{
								return error;
							}
						}
						else
						{
							_response_body->Append(sub_data);
							processed_bytes = sub_data->GetLength();
						}

						OV_ASSERT2(remained >= processed_bytes);
						remained -= processed_bytes;

						break;
					}

					case StatusCode::PartialContent: {
						auto processed_length = _parser.AppendData(sub_data);

						switch (_parser.GetStatus())
						{
							case StatusCode::OK:
								// Parsing just completed
								// After parsing is completed, this code is executed only once
								PostProcess();
								break;

							case StatusCode::PartialContent:
								// We need more data to parse response
								// In this state, all data must be processed
								OV_ASSERT2((processed_length >= 0LL) && (static_cast<size_t>(processed_length) == data->GetLength()));
								break;

							default:
								OV_ASSERT2(processed_length == -1L);
								return ov::Error::CreateError("HTTP", "Could not parse response");
						}

						sub_data = data->Subdata(processed_length);
						remained -= processed_length;
						break;
					}

					default:
						// If an error occurred when parsing before, this code should not executed under normal circumstances because it was closed after responed.
						OV_ASSERT2(false);
						return ov::Error::CreateError("HTTP", "Invalid parse status: %d", _parser.GetStatus());
				}
			}

			return nullptr;
		}

		bool HttpClient::SendData(const std::shared_ptr<const ov::Data> &data)
		{
			if (data == nullptr)
			{
				// Nothing to do
				return true;
			}

			if (_tls_data != nullptr)
			{
				return _tls_data->Encrypt(data);
			}

			return _socket->Send(data);
		}

		void HttpClient::PostProcess()
		{
			logtt("Allocating %zu bytes for receiving response data", _parser.GetContentLength());

			_response_body = std::make_shared<ov::Data>(_parser.GetContentLength());

			_is_chunked_transfer = (_parser.GetHeader("TRANSFER-ENCODING") == "chunked");

			if (_is_chunked_transfer)
			{
				_chunk_parse_status = ChunkParseStatus::Header;
				_chunk_header.SetCapacity(16);
				_chunk_length = 0L;
			}
		}

		void HttpClient::CleanupVariables()
		{
			// Clean up variables
			_url.Clear();
			_parsed_url = nullptr;
			_response_handler = nullptr;

			OV_SAFE_RESET(
				_tls_data, nullptr, {
					_tls_data->SetIoCallback(nullptr);
					_tls_data = nullptr;
				},
				_tls_data);
			OV_SAFE_RESET(_socket, nullptr, _socket->Close(), _socket);
		}

		void HttpClient::HandleError(std::shared_ptr<const ov::Error> error)
		{
			auto response_handler = _response_handler;

			// An error occurred - reset all variables
			CleanupVariables();

			if (response_handler != nullptr)
			{
				auto http_error = std::dynamic_pointer_cast<const HttpError>(error);

				if (http_error != nullptr)
				{
					// An error occurred in some modules related to HTTP
					response_handler(http_error->GetStatusCode(), _response_body, error);
				}
				else
				{
					response_handler(StatusCode::Unknown, _response_body, error);
				}
			}
		}
	}  // namespace clnt
}  // namespace http
