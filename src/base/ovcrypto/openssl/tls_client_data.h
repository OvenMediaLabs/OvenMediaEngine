//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./tls.h"
#include "openssl_error.h"

namespace ov
{
	class TlsClientDataIoCallback
	{
	public:
		virtual ssize_t OnTlsReadData(void *data, int64_t length) = 0;
		virtual ssize_t OnTlsWriteData(const void *data, int64_t length) = 0;
	};

	class TlsClientData
	{
	public:
		enum class State
		{
			Invalid,

			WaitingForConnect,
			Connecting,
			Connected,
		};

		TlsClientData(const std::shared_ptr<TlsContext> &tls_context, bool is_nonblocking);

		~TlsClientData();

		State GetState() const
		{
			return _state;
		}

		void SetIoCallback(const std::shared_ptr<TlsClientDataIoCallback> &callback);
		// For SNI
		void SetTlsHostName(const ov::String &host_name);

		std::shared_ptr<const OpensslError> Connect();

		/// Read via _io_callback->OnTlsReadData() and decrypt it, and returns it
		///
		/// @param plain_data decrypted data if there is no error
		/// @returns Returns true if read successfully, false if an error occurred
		bool Decrypt(std::shared_ptr<const Data> *plain_data);
		// Encrypt plain_data, and send via _io_callback->OnTlsWriteData()
		bool Encrypt(const std::shared_ptr<const Data> &plain_data);

		size_t GetDataLength() const;
		std::shared_ptr<const Data> GetData() const;

	protected:
		//--------------------------------------------------------------------
		// Called by TLS module
		//--------------------------------------------------------------------
		// Tls.Read() -> SSL_read() -> Tls::TlsRead() -> TlsBioCallback.read_callback -> TlsClientData.OnTlsRead()
		ssize_t OnTlsRead(Tls *tls, void *buffer, size_t length);
		// Tls.Write() -> SSL_write() -> Tls::TlsWrite() -> TlsBioCallback.write_callback -> TlsClientData.OnTlsWrite()
		ssize_t OnTlsWrite(Tls *tls, const void *data, size_t length);
		// OpenSSL -> Tls::() -> Tls::TlsCtrl() -> TlsBioCallback.ctrl_callback -> TlsClientData.OnTlsCtrl()
		long OnTlsCtrl(ov::Tls *tls, int cmd, long num, void *arg);

	protected:
		State _state = State::Invalid;

		std::shared_ptr<TlsClientDataIoCallback> _io_callback;

		Tls _tls;
	};
}  // namespace ov