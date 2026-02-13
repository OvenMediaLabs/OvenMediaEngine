//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "tls_server_data.h"

#include "./openssl_private.h"

namespace ov
{
	TlsServerData::TlsServerData(const std::shared_ptr<TlsContext> &tls_context, bool is_nonblocking)
	{
		TlsBioCallback callback = {
			.read_callback = std::bind(&TlsServerData::OnTlsRead, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
			.write_callback = std::bind(&TlsServerData::OnTlsWrite, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
			.destroy_callback = nullptr,
			.ctrl_callback = std::bind(&TlsServerData::OnTlsCtrl, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)};

		bool result = _tls.Initialize(tls_context, callback, is_nonblocking);

		if (result)
		{
			_state = State::WaitingForAccept;
		}
		else
		{
			auto error = OpensslError();

			logte("Could not initialize TLS: %s", error.What());
			_state = State::Invalid;
		}
	}

	TlsServerData::~TlsServerData()
	{
		_tls.Uninitialize();
	}

	bool TlsServerData::Decrypt(const std::shared_ptr<const Data> &cipher_data, std::shared_ptr<const Data> *plain_data)
	{
		if (_state == State::Invalid)
		{
			logtt("Invalid state");
			return false;
		}

		logtt("Trying to decrypt the data for TLS\n%s", cipher_data->Dump(32).CStr());

		{
			std::lock_guard lock_guard(_cipher_data_mutex);
			if ((_cipher_data == nullptr) || _cipher_data->IsEmpty())
			{
				_cipher_data = cipher_data->Clone();
			}
			else
			{
				// Append the data
				if (_cipher_data->Append(cipher_data) == false)
				{
					logtt("Could not append data");
					return false;
				}
			}
		}

		volatile bool stop = false;
		std::shared_ptr<Data> decrypted;

		while (stop == false)
		{
			switch (_state)
			{
				case State::Invalid:
					OV_ASSERT2(false);
					return false;

				case State::WaitingForAccept: {
					logtt("Trying to accept TLS...");

					int result = _tls.Accept();

					switch (result)
					{
						case SSL_ERROR_NONE: {
							logtt("Accepted");
							_state = State::Accepted;
							break;
						}

						case SSL_ERROR_WANT_READ:
							logtt("Need more data to accept the request");
							stop = true;
							break;

						default:
							logtt("An error occurred while accept TLS connection: error code: %d", result);
							return false;
					}

					break;
				}

				case State::Accepted: {
					logtt("Trying to read data from TLS module...");

					auto read_data = _tls.Read();

					if (read_data == nullptr)
					{
						logtt("An error occurred while read TLS data");
						return false;
					}

					if (read_data->IsEmpty())
					{
						stop = true;
					}
					else
					{
						logtt("Decrypted data\n%s", read_data->Dump().CStr());

						if (decrypted != nullptr)
						{
							decrypted->Append(read_data);
						}
						else
						{
							decrypted = read_data;
						}
					}

					break;
				}
			}
		}

		if (plain_data != nullptr)
		{
			*plain_data = decrypted;
		}

		return true;
	}

	bool TlsServerData::Encrypt(const std::shared_ptr<const Data> &plain_data, std::shared_ptr<const Data> *cipher_data)
	{
		if (_state != State::Accepted)
		{
			// Before encrypting data, key exchange must be done first
			logtt("Invalid state: %d", ov::ToUnderlyingType(_state));
			return false;
		}

		logtt("Trying to encrypt the data for TLS\n%s", plain_data->Dump(32).CStr());

		size_t written_bytes = 0;
		auto result = _tls.Write(plain_data, &written_bytes);
		if (result == SSL_ERROR_NONE)
		{
			std::lock_guard lock_guard(_plain_data_mutex);
			*cipher_data = std::move(_plain_data);
			return true;
		}
		else
		{
			logtt("An error occurred while encrypting data: data_len(%zu), error code: %d", plain_data->GetLength(), result);
		}

		return false;
	}

	TlsServerData::AlpnProtocol TlsServerData::GetSelectedAlpnProtocol() const
	{
		auto alpn_protocol = _tls.GetSelectedAlpnName();

		// https://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml#alpn-protocol-ids

		if (alpn_protocol.IsEmpty())
		{
			return AlpnProtocol::None;
		}
		else if (alpn_protocol == "h2")
		{
			return AlpnProtocol::Http20;
		}
		else if (alpn_protocol == "http/1.1")
		{
			return AlpnProtocol::Http11;
		}
		else if (alpn_protocol == "http/1.0")
		{
			return AlpnProtocol::Http10;
		}

		logtw("Unknown ALPN protocol: %s", alpn_protocol.CStr());

		return AlpnProtocol::Unsupported;
	}

	ov::String TlsServerData::GetSelectedAlpnProtocolStr() const
	{
		return _tls.GetSelectedAlpnName();
	}

	ssize_t TlsServerData::OnTlsRead(Tls *tls, void *buffer, size_t length)
	{
		std::lock_guard lock_guard(_cipher_data_mutex);

		if (_cipher_data == nullptr)
		{
			return 0;
		}

		auto bytes_to_copy = std::min(length, _cipher_data->GetLength());

		logtt("Trying to read %zu bytes from TLS data buffer (length: %zu, data: %zu)...", bytes_to_copy, length, _cipher_data->GetLength());

		::memcpy(buffer, _cipher_data->GetData(), bytes_to_copy);
		_cipher_data = (_cipher_data->GetLength() == bytes_to_copy) ? nullptr : _cipher_data->Subdata(bytes_to_copy);

		logtt("%zu bytes is remained in TLS data buffer", (_cipher_data != nullptr) ? _cipher_data->GetLength() : 0);

		return bytes_to_copy;
	}

	ssize_t TlsServerData::OnTlsWrite(Tls *tls, const void *data, size_t length)
	{
		if (_state == State::WaitingForAccept)
		{
			if (_write_callback != nullptr)
			{
				return _write_callback(data, length);
			}

			OV_ASSERT2(false);
			return -1LL;
		}
		else
		{
			std::lock_guard lock_guard(_plain_data_mutex);
			if (_plain_data == nullptr)
			{
				_plain_data = std::make_shared<Data>(data, length);
			}
			else
			{
				_plain_data->Append(data, length);
			}
		}

		return length;
	}

	long TlsServerData::OnTlsCtrl(Tls *tls, int cmd, long num, void *arg)
	{
		logtt("[TLS] Ctrl: %d, %ld, %p", cmd, num, arg);

		switch (cmd)
		{
			case BIO_CTRL_RESET:
			case BIO_CTRL_WPENDING:
			case BIO_CTRL_PENDING:
				return 0;

			case BIO_CTRL_FLUSH:
				return 1;

			default:
				return 0;
		}
	}
}  // namespace ov