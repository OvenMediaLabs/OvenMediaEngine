#include "dtls_transport.h"

#include <algorithm>
#include <utility>

#define OV_LOG_TAG "DTLS"

DtlsTransport::DtlsTransport()
	: ov::Node(NodeType::Dtls)
{
	_state = SSL_NONE;
	_peer_certificate_verified = false;
}

DtlsTransport::~DtlsTransport()
{
}

bool DtlsTransport::Stop()
{
	std::lock_guard<std::mutex> lock(_tls_lock);

	_tls.Uninitialize();

	return ov::Node::Stop();
}

// Set Local Certificate
void DtlsTransport::SetLocalCertificate(const std::shared_ptr<::Certificate> &certificate)
{
	_local_certificate = certificate;

	ov::TlsContextCallback tls_context_callback = {
		.create_callback = [](ov::TlsContext *tls_context, SSL_CTX *context) -> bool {
			tls_context->SetVerify(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT);

			// SSL_CTX_set_tlsext_use_srtp() returns 1 on error, 0 on success
			if (::SSL_CTX_set_tlsext_use_srtp(context, "SRTP_AEAD_AES_128_GCM:SRTP_AES128_CM_SHA1_80:SRTP_AES128_CM_SHA1_32"))
			{
				logte("SSL_CTX_set_tlsext_use_srtp failed");
				return false;
			}

			return true;
		},

		.verify_callback = [](ov::TlsContext *tls, X509_STORE_CTX *store_context) -> bool {
			return true;
		}};

	std::shared_ptr<const ov::Error> error;
	_tls_context = ov::TlsContext::CreateServerContext(
		ov::TlsMethod::DTls,
		_local_certificate,
		"DEFAULT:!NULL:!aNULL:!SHA256:!SHA384:!aECDH:!AESGCM+AES256:!aPSK",
		false,
		false,
		&tls_context_callback,
		&error);

	if (error != nullptr)
	{
		logte("Could not append certificate: %s", error->What());
	}
}

// Set Peer Fingerprint for verification
void DtlsTransport::SetPeerFingerprint(ov::String algorithm, ov::String fingerprint)
{
	_peer_fingerprint_algorithm = algorithm;
	_peer_fingerprint_value = fingerprint;
}

// Start DTLS
bool DtlsTransport::StartDTLS()
{
	std::lock_guard<std::mutex> lock(_tls_lock);

	/*
	if(_ice_port->GetState() != IcePortConnectionState::Completed)
	{
		_state = SSL_WAIT;
		return false;
	}
	*/

	ov::TlsBioCallback tls_bio_callback =
		{
			.read_callback = std::bind(&DtlsTransport::Read, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
			.write_callback = std::bind(&DtlsTransport::Write, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
			.destroy_callback = nullptr,
			.ctrl_callback = [](ov::Tls *tls, int cmd, long num, void *ptr) -> long {
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
			}};

	if (_tls.Initialize(_tls_context, tls_bio_callback, true) == false)
	{
		_state = SSL_ERROR;
		return false;
	}

	_state = SSL_CONNECTING;

	ContinueSSL();

	return true;
}

bool DtlsTransport::ContinueSSL()
{
	logtt("Continue DTLS...");
	int error = _tls.Accept();

	if (error == SSL_ERROR_NONE)
	{
		_state = SSL_CONNECTED;

		_peer_certificate = _tls.GetPeerCertificate();

		if (_peer_certificate == nullptr)
		{
			return false;
		}

		if (VerifyPeerCertificate() == false)
		{
			logte("Session could not verify peer certificate");
			return false;
		}

		_peer_certificate->Print();

		return MakeSrtpKey();
	}

	return false;
}

bool DtlsTransport::MakeSrtpKey()
{
	if (_peer_certificate_verified == false)
	{
		return false;
	}

	// https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#exporter-labels
	const ov::String label = "EXTRACTOR-dtls_srtp";

	auto crypto_suite = _tls.GetSelectedSrtpProfileId();

	std::shared_ptr<ov::Data> server_key = std::make_shared<ov::Data>();
	std::shared_ptr<ov::Data> client_key = std::make_shared<ov::Data>();

	_tls.ExportKeyingMaterial(crypto_suite, label, server_key, client_key);

	auto node = GetPrevNode();
	if (node->GetNodeType() == NodeType::Srtp)
	{
		auto srtp_transport = std::static_pointer_cast<SrtpTransport>(node);
		srtp_transport->SetKeyMaterial(crypto_suite, server_key, client_key);
	}

	return true;
}

bool DtlsTransport::VerifyPeerCertificate()
{
	// TODO(Getroot): Compare and verify the digest received from the PEER CERTIFICATE and SDP.
	logtt("Accepted peer certificate");
	_peer_certificate_verified = true;
	return true;
}

bool DtlsTransport::OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data)
{
	if (GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

	switch (_state)
	{
		case SSL_NONE:
		case SSL_WAIT:
		case SSL_CONNECTING:
			break;
		case SSL_CONNECTED:
			// Since SRTP is already encrypted, it is sent directly to ICE.
			if (from_node == NodeType::Srtp)
			{
				return SendDataToNextNode(data);
			}
			else
			{
				std::lock_guard<std::mutex> lock(_tls_lock);
				// If it is not SRTP, it must be encrypted in DTLS.
				// TODO: Currently, SCTP is not supported, so there is no need to encrypt,
				// and it will be developed if it supports data channels in the future.
				size_t written_bytes;
				int ssl_error = _tls.Write(data->GetData(), data->GetLength(), &written_bytes);
				if (ssl_error == SSL_ERROR_NONE)
				{
					return true;
				}

				logte("SSL_write error : error(%d)", ssl_error);
			}
			break;
		case SSL_ERROR:
		case SSL_CLOSED:
			break;
		default:
			break;
	}

	return false;
}

// IcePort -> Publisher ->[queue] Application {thread}-> Session -> DtlsTransport -> SRTP || SCTP
bool DtlsTransport::OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data)
{
	if (GetNodeState() != ov::Node::NodeState::Started)
	{
		logtt("Node has not started, so the received data has been canceled.");
		return false;
	}

	logtt("OnDataReceived (%d) bytes", data->GetLength());

	switch (_state)
	{
		case SSL_NONE:
		case SSL_WAIT:
			break;
		case SSL_CONNECTING:
		case SSL_CONNECTED: {
			if (IsDtlsPacket(data))
			{
				std::lock_guard<std::mutex> lock(_tls_lock);
				logtt("Receive DTLS packet");
				// Packet을 Queue에 쌓는다.
				SaveDtlsPacket(data);

				if (_state == SSL_CONNECTING)
				{
					ContinueSSL();
				}
				else
				{
					char buffer[MAX_DTLS_PACKET_LEN];

					// SSL -> Read() -> TakeDtlsPacket() -> Decrypt -> buffer
					[[maybe_unused]] int ssl_error = _tls.Read(buffer, sizeof(buffer), nullptr);

					int pending = _tls.Pending();
					if (pending >= 0)
					{
						logtt("Short DTLS read. Flushing %d bytes", pending);
						_tls.FlushInput();
					}

					// TODO: Currently, SCTP is not supported, so there is no need to encrypt,
					// and it will be developed if it supports data channels in the future.
					logtt("Unknown dtls packet received (%d)", ssl_error);
				}

				return true;
			}
			// SRTP or SRTCP will be input here. However, since OME does not receive media,
			// SRTP cannot be input, only SRTCP can be input.
			else
			{
				return SendDataToPrevNode(data);
			}
			break;
		}
		case SSL_ERROR:
		case SSL_CLOSED:
			break;
		default:
			break;
	}

	return false;
}

ssize_t DtlsTransport::Read(ov::Tls *tls, void *buffer, size_t length)
{
	std::shared_ptr<const ov::Data> data = TakeDtlsPacket();

	if (data == nullptr)
	{
		logtt("SSL read packet block");
		return 0;
	}

	size_t read_len = std::min<size_t>(length, static_cast<size_t>(data->GetLength()));

	logtt("SSL read packet : %zu", read_len);

	::memcpy(buffer, data->GetData(), read_len);

	return read_len;
}

ssize_t DtlsTransport::Write(ov::Tls *tls, const void *data, size_t length)
{
	auto packet = std::make_shared<ov::Data>(data, length);

	logtt("SSL write packet : %zu", packet->GetLength());

	if (SendDataToNextNode(packet))
	{
		return length;
	}

	// retry
	return 0;
}

bool DtlsTransport::IsDtlsPacket(const std::shared_ptr<const ov::Data> data)
{
	//TODO: DTLS처럼 보이는 쓰레기가 있는지 더 엄격하게 검사한다.
	uint8_t *buffer = (uint8_t *)data->GetData();
	return (data->GetLength() >= DTLS_RECORD_HEADER_LEN && (buffer[0] > 19 && buffer[0] < 64));
}

bool DtlsTransport::IsRtpPacket(const std::shared_ptr<const ov::Data> data)
{
	uint8_t *buffer = (uint8_t *)data->GetData();
	return (data->GetLength() >= MIN_RTP_PACKET_LEN && (buffer[0] & 0xC0) == 0x80);
}

bool DtlsTransport::SaveDtlsPacket(const std::shared_ptr<const ov::Data> data)
{
	// 이미 하나가 저장되어 있는데 또 저장하려는 것은 잘못된 것이다.
	// 구조상 저장하자마자 사용해야 한다.
	if (_packet_buffer.size() == 1)
	{
		logte("Ssl buffer is full");
		return false;
	}

	_packet_buffer.push_back(data);

	return true;
}

std::shared_ptr<const ov::Data> DtlsTransport::TakeDtlsPacket()
{
	if (_packet_buffer.size() <= 0)
	{
		logtt("Ssl buffer is empty");
		return nullptr;
	}

	std::shared_ptr<const ov::Data> data = _packet_buffer.front();
	_packet_buffer.pop_front();
	return data;
}
