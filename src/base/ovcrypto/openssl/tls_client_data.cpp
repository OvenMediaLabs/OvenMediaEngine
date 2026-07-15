//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "tls_client_data.h"

#include "./openssl_private.h"

namespace ov
{
	TlsClientData::TlsClientData(const std::shared_ptr<TlsContext> &tls_context, bool is_nonblocking)
	{
		TlsBioCallback callback =
			{
				.read_callback = std::bind(&TlsClientData::OnTlsRead, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
				.write_callback = std::bind(&TlsClientData::OnTlsWrite, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
				.destroy_callback = nullptr,
				.ctrl_callback = std::bind(&TlsClientData::OnTlsCtrl, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)};

		if (_tls.Initialize(tls_context, callback, is_nonblocking))
		{
			_state = State::WaitingForConnect;
		}
		else
		{
			auto error = OpensslError();
			logte("Could not initialize TLS: %s", error.What());
			_state = State::Invalid;
		}
	}

	TlsClientData::~TlsClientData()
	{
		_tls.Uninitialize();
	}

	void TlsClientData::SetIoCallback(const std::shared_ptr<TlsClientDataIoCallback> &callback)
	{
		_io_callback = callback;
	}

	void TlsClientData::SetTlsHostName(const ov::String &host_name)
	{
		_tls.SetTlsHostName(host_name);
	}

	std::shared_ptr<const OpensslError> TlsClientData::Connect()
	{
		_state = State::Connecting;

		auto error = _tls.Connect();

		if (error == nullptr)
		{
			logtt("Subject: %s, Issuer: %s", _tls.GetSubjectName().CStr(), _tls.GetIssuerName().CStr());
			_state = State::Connected;
		}

		return error;
	}

	bool TlsClientData::Decrypt(std::shared_ptr<const Data> *plain_data)
	{
		if (_state == State::Invalid)
		{
			logtt("Invalid state");
			return false;
		}

		auto data = _tls.Read();

		if (plain_data != nullptr)
		{
			*plain_data = data;
		}

		return (data != nullptr);
	}

	bool TlsClientData::Encrypt(const std::shared_ptr<const Data> &plain_data)
	{
		if (_state != State::Connected)
		{
			// Before encrypting data, key exchange must be done first
			logtt("Invalid state: %d", ToUnderlyingType(_state));
			return false;
		}

		logtp("Trying to encrypt the data for TLS\n%s", plain_data->Dump(32).CStr());

		size_t written_bytes = 0;

		// Tls::Write() -> SSL_write() -> Tls::TlsWrite() -> BIO_get_data()::write_callback -> TlsClientData::OnTlsWrite()
		if (_tls.Write(plain_data, &written_bytes) == SSL_ERROR_NONE)
		{
			return true;
		}

		return false;
	}

	ssize_t TlsClientData::OnTlsRead(Tls *tls, void *buffer, size_t length)
	{
		logtp("Trying to read %zu bytes from I/O callback...", length);

		if (_io_callback != nullptr)
		{
			return _io_callback->OnTlsReadData(buffer, length);
		}

		return -1;
	}

	ssize_t TlsClientData::OnTlsWrite(Tls *tls, const void *data, size_t length)
	{
		logtp("Trying to send encrypted data to I/O callback\n%s", ov::Dump(data, length, 32).CStr());

		if (_io_callback != nullptr)
		{
			return _io_callback->OnTlsWriteData(data, length);
		}

		return -1;
	}

	long TlsClientData::OnTlsCtrl(Tls *tls, int cmd, long num, void *arg)
	{
		logtp("[TLS] Ctrl: %d, %ld, %p", cmd, num, arg);

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