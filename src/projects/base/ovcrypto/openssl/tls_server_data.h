//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./tls.h"
#include "openssl_error.h"

namespace ov
{
	class TlsServerData
	{
	public:
		using WriteCallback = std::function<ssize_t(const void *data, int64_t length)>;

		enum class State
		{
			Invalid,
			WaitingForAccept,
			Accepted,
		};

		enum class AlpnProtocol : uint8_t
		{
			None,
			Http10,
			Http11,
			Http20,
			Unsupported,
		};

		TlsServerData(const std::shared_ptr<TlsContext> &tls_context, bool is_blocking);

		~TlsServerData();

		State GetState() const
		{
			return _state;
		}

		const WriteCallback &GetWriteCallback() const
		{
			return _write_callback;
		}

		// This callback is called when TLS negotiation is in progress
		void SetWriteCallback(WriteCallback write_callback)
		{
			_write_callback = write_callback;
		}

		ov::String GetServerName() const
		{
			return _tls.GetServerName();
		}

		const Tls &GetTls() const
		{
			return _tls;
		}

		// Get ALPN protocol
		AlpnProtocol GetSelectedAlpnProtocol() const;
		ov::String GetSelectedAlpnProtocolStr() const;
		
		// plain_data can be null even if successful (It indicates accepting a new client)
		bool Decrypt(const std::shared_ptr<const Data> &cipher_data, std::shared_ptr<const Data> *plain_data);
		// cipher_data can be null even if successful (It indicates accepting a new client)
		bool Encrypt(const std::shared_ptr<const Data> &plain_data, std::shared_ptr<const Data> *cipher_data);

		size_t GetDataLength() const;
		std::shared_ptr<const Data> GetData() const;

		std::mutex& GetSequentialSendMutex()
		{
			return _tls_sequential_send_mutex;
		}

	protected:
		//--------------------------------------------------------------------
		// Called by TLS module
		//--------------------------------------------------------------------
		// Tls.Read() -> SSL_read() -> Tls::TlsRead() -> TlsBioCallback.read_callback -> TlsServerData.OnTlsRead()
		ssize_t OnTlsRead(Tls *tls, void *buffer, size_t length);
		// Tls.Write() -> SSL_write() -> Tls::TlsWrite() -> TlsBioCallback.write_callback -> TlsServerData.OnTlsWrite()
		ssize_t OnTlsWrite(Tls *tls, const void *data, size_t length);
		// OpenSSL -> Tls::() -> Tls::TlsCtrl() -> TlsBioCallback.ctrl_callback -> TlsServerData.OnTlsCtrl()
		long OnTlsCtrl(ov::Tls *tls, int cmd, long num, void *arg);

	protected:
		State _state = State::Invalid;

		ov::String _server_name;

		Tls _tls;
		WriteCallback _write_callback;

		std::mutex _cipher_data_mutex;
		std::shared_ptr<Data> _cipher_data;

		std::mutex _plain_data_mutex;
		std::shared_ptr<Data> _plain_data;

		AlpnProtocol _selected_alpn_protocol = AlpnProtocol::Http11;

	private:
		std::mutex _tls_sequential_send_mutex;	// for atomic send
	};
}  // namespace ov
