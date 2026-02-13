//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_response.h"

#include <base/ovsocket/ovsocket.h>

#include <algorithm>
#include <memory>
#include <utility>
#include "config/config_manager.h"

#include "./http_server_private.h"

namespace http
{
	namespace svr
	{
		HttpResponse::HttpResponse(const std::shared_ptr<ov::ClientSocket> &client_socket)
			: _client_socket(client_socket)
		{
			OV_ASSERT2(_client_socket != nullptr);

			_created_time = std::chrono::system_clock::now();

			auto module_config = cfg::ConfigManager::GetInstance()->GetServer()->GetModules();
			_etag_enabled_by_config = module_config.GetETag().IsEnabled();
		}

		HttpResponse::HttpResponse(const std::shared_ptr<HttpResponse> &http_response)
		{
			OV_ASSERT2(http_response != nullptr);

			_client_socket = http_response->_client_socket;
			_tls_data = http_response->_tls_data;
			_status_code = http_response->_status_code;
			_reason = http_response->_reason;
			_is_header_sent = http_response->_is_header_sent;
			_response_header = http_response->_response_header;
			_response_data_list = http_response->_response_data_list;
			_response_data_size = http_response->_response_data_size;
			_default_value = http_response->_default_value;
			_created_time = http_response->_created_time;
		}

		void HttpResponse::SetTlsData(const std::shared_ptr<ov::TlsServerData> &tls_data)
		{
			_tls_data = tls_data;
		}

		std::shared_ptr<ov::ClientSocket> HttpResponse::GetRemote()
		{
			return _client_socket;
		}

		std::shared_ptr<const ov::ClientSocket> HttpResponse::GetRemote() const
		{
			return _client_socket;
		}

		std::shared_ptr<ov::TlsServerData> HttpResponse::GetTlsData()
		{
			return _tls_data;
		}

		void HttpResponse::SetMethod(Method method)
		{
			_method = method;
		}

		Method HttpResponse::GetMethod() const
		{
			return _method;
		}

		void HttpResponse::SetIfNoneMatch(const ov::String &etag)
		{
			_if_none_match = etag;
		}

		const ov::String &HttpResponse::GetIfNoneMatch() const
		{
			return _if_none_match;
		}

		StatusCode HttpResponse::GetStatusCode() const
		{
			return _status_code;
		}

		// Get Reason
		ov::String HttpResponse::GetReason() const
		{
			return _reason;
		}

		// reason = default
		void HttpResponse::SetStatusCode(StatusCode status_code)
		{
			SetStatusCode(status_code, StringFromStatusCode(status_code));
		}

		// custom reason
		void HttpResponse::SetStatusCode(StatusCode status_code, const ov::String &reason)
		{
			_status_code = status_code;
			_reason = reason;
		}

		bool HttpResponse::AddHeader(const ov::String &key, const ov::String &value)
		{
			if (IsHeaderSent())
			{
				logtw("Cannot add header: Header is sent: %s", _client_socket->ToString().CStr());
				return false;
			}
			
			_response_header[key].push_back(value);

			return true;
		}

		bool HttpResponse::SetHeader(const ov::String &key, const ov::String &value)
		{
			if (IsHeaderSent())
			{
				logtw("Cannot set header: Header is sent: %s", _client_socket->ToString().CStr());
				return false;
			}
			
			_response_header[key] = {value};

			return true;
		}

		bool HttpResponse::UnsetHeader(const ov::String &key)
		{
			if (IsHeaderSent())
			{
				logtw("Cannot unset header: Header is sent: %s", _client_socket->ToString().CStr());
				return false;
			}

			auto response_iterator = _response_header.find(key);
			if (response_iterator == _response_header.end())
			{
				return false;
			}

			_response_header.erase(response_iterator);

			return true;
		}

		const std::vector<ov::String> &HttpResponse::GetHeader(const ov::String &key) const
		{
			auto item = _response_header.find(key);

			if (item == _response_header.end())
			{
				return _default_value;
			}

			return item->second;
		}

		bool HttpResponse::RemoveHeader(const ov::String &key)
		{
			if (IsHeaderSent())
			{
				logtw("Cannot remove header: Header is sent: %s", _client_socket->ToString().CStr());
				return false;
			}

			auto response_iterator = _response_header.find(key);

			if (response_iterator == _response_header.end())
			{
				return false;
			}

			_response_header.erase(response_iterator);

			return true;
		}

		ov::String HttpResponse::GetEtag()
		{
			if (_response_hash == nullptr)
			{
				return "";
			}

			return ov::String::FormatString("%s-%d", _response_hash->ToHexString().CStr(), _response_data_size);
		}

		bool HttpResponse::AppendData(const std::shared_ptr<const ov::Data> &data)
		{
			if (data == nullptr)
			{
				return false;
			}

			std::lock_guard<decltype(_response_mutex)> lock(_response_mutex);

			auto cloned_data = data->Clone();

			_response_data_list.push_back(cloned_data);
			_response_data_size += cloned_data->GetLength();

			if (_etag_enabled_by_config == false)
			{
				return true;
			}

			auto md5 = ov::MessageDigest::ComputeDigest(ov::CryptoAlgorithm::Md5, cloned_data);
			if (md5 == nullptr || md5->GetLength() != 16)
			{
				// Could not compute MD5
				OV_ASSERT2(md5->GetLength() == 16);
				return true;
			}

			if (_response_hash == nullptr)
			{
				_response_hash = md5;
			}
			else
			{
				// Update hash, xor with previous hash
				for (size_t i = 0; i < md5->GetLength(); i++)
				{
					auto ptr = _response_hash->GetWritableDataAs<uint8_t>();
					ptr[i] ^= md5->At(i);
				}
			}

			return true;
		}

		bool HttpResponse::AppendString(const ov::String &string)
		{
			return AppendData(string.ToData(false));
		}

		bool HttpResponse::AppendFile(const ov::String &filename)
		{
			OV_ASSERT(false, "Not implemented");
			return false;
		}

		bool HttpResponse::IsHeaderSent() const
		{
			return _is_header_sent;
		}

		// Get Response Data Size
		size_t HttpResponse::GetResponseDataSize() const
		{
			return _response_data_size;
		}

		// Get Response Data List
		const std::vector<std::shared_ptr<const ov::Data>> &HttpResponse::GetResponseDataList() const
		{
			return _response_data_list;
		}

		// Get Response Header
		const std::unordered_map<ov::String, std::vector<ov::String>, ov::CaseInsensitiveHash, ov::CaseInsensitiveEqual> &HttpResponse::GetResponseHeaderList() const
		{
			return _response_header;
		}

		void HttpResponse::ResetResponseData()
		{
			_response_data_list.clear();
			_response_data_size = 0ULL;
		}

		// Get Created Time
		std::chrono::system_clock::time_point HttpResponse::GetCreatedTime() const
		{
			return _created_time;
		}

		// Get Resopnsed Time
		std::chrono::system_clock::time_point HttpResponse::GetResponseTime() const
		{
			return _response_time;
		}
		
		// Get Sent size
		uint32_t HttpResponse::GetSentSize() const
		{
			return _sent_size;
		}

		int32_t HttpResponse::Response()
		{
			std::lock_guard<decltype(_response_mutex)> lock(_response_mutex);
			_response_time = std::chrono::system_clock::now();

			uint32_t sent_size = 0;
			bool only_header = false;

			if (IsHeaderSent() == false)
			{
				// Date header
				auto date = ov::Converter::ToRFC7231String(_response_time);
				SetHeader("Date", date);

				if (_etag_enabled_by_config == true)
				{
					// IF-NONE-MATCH check
					auto if_none_match = GetIfNoneMatch();
					if (if_none_match.IsEmpty() == false)
					{
						if (if_none_match == GetEtag())
						{
							SetStatusCode(StatusCode::NotModified);
							only_header = true;
						}
					}

					if (GetStatusCode() == StatusCode::OK || GetStatusCode() == StatusCode::NotModified)
					{
						auto etag_value = GetEtag();
						if (etag_value.IsEmpty() == false)
						{
							SetHeader("ETag", etag_value);
						}
					}
				}
				
				auto sent_header_size = SendHeader();
				// Header must be bigger than 0, if header is not sent, it is an error
				if (sent_header_size <= 0)
				{
					return -1;
				}

				sent_size += sent_header_size;
				if (sent_size > 0)
				{
					_is_header_sent = true;
				}
			}

			if (GetMethod() == Method::Head || only_header == true)
			{
				_sent_size += sent_size;
				return sent_size;
			}

			auto sent_data_size = SendPayload();
			if (sent_data_size < 0)
			{
				return -1;
			}

			sent_size += sent_data_size;

			_sent_size += sent_size;

			return sent_size;
		}	

		int32_t HttpResponse::SendHeader()
		{
			return -1;
		}

		int32_t HttpResponse::SendPayload()
		{
			return 0;
		}

		bool HttpResponse::Send(const void *data, size_t length)
		{
			return Send(std::make_shared<ov::Data>(data, length));
		}

		bool HttpResponse::Send(const std::shared_ptr<const ov::Data> &data)
		{
			if (data == nullptr)
			{
				OV_ASSERT2(data != nullptr);
				return false;
			}

			std::shared_ptr<const ov::Data> send_data;

			if (_tls_data == nullptr)
			{
				send_data = data->Clone();
			}
			else
			{
				std::lock_guard<std::mutex> lock(_tls_data->GetSequentialSendMutex());

				if (_tls_data->Encrypt(data, &send_data) == false)
				{
					logte("Failed to encrypt data: %s", _client_socket->ToString().CStr());
					return false;
				}

				if ((send_data == nullptr) || send_data->IsEmpty())
				{
					// There is no data to send
					return true;
				}

				auto result = _client_socket->Send(send_data);

				return result;
			}

			return _client_socket->Send(send_data);
		}

		bool HttpResponse::Close()
		{
			OV_ASSERT2(_client_socket != nullptr);

			if (_client_socket == nullptr)
			{
				return false;
			}

			return _client_socket->CloseIfNeeded();
		}

		ov::String HttpResponse::ToString() const
		{
			ov::String output;

			output.AppendFormat("<HttpResponse> Status(%d) Reason(%s) Header(%zu) Data(%zu)\n",
								GetStatusCode(),
								GetReason().CStr(),
								_response_header.size(),
								_response_data_size);

			output.AppendFormat("\n[Header]\n");
			for (auto &[key, values] : _response_header)
			{	
				for (auto &value : values)
				{
					output.AppendFormat("%s: %s\n", key.CStr(), value.CStr());
				}
			}

			return output;
		}
	}  // namespace svr
}  // namespace http
