#pragma once

#include <base/ovlibrary/node.h>
#include <base/ovcrypto/ovcrypto.h>
#include <base/common_types.h>

#include "modules/ice/ice_port.h"
#include "srtp_transport.h"

#define DTLS_RECORD_HEADER_LEN                  13
#define MAX_DTLS_PACKET_LEN                     2048
#define MIN_RTP_PACKET_LEN                      12

class DtlsTransport : public ov::Node
{
public:
	// Send : Srtp -> this -> Ice
	// Recv : Ice -> {[Queue] -> Application -> Session} -> this -> Srtp
	explicit DtlsTransport();
	virtual ~DtlsTransport();

	// Set Local Certificate
	void SetLocalCertificate(const std::shared_ptr<Certificate> &certificate);

	// Set Peer Fingerprint for verification
	void SetPeerFingerprint(ov::String algorithm, ov::String fingerprint);

	// Start DTLS
	bool StartDTLS();

	bool Stop() override;
	//--------------------------------------------------------------------
	// Implementation of Node
	//--------------------------------------------------------------------
	// Receive data from upper node, and send data to lower node.
	bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) override;
	// Receive data from lower node, and send data to upper node.
	bool OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data) override;

	// IcePort -> Publisher ->[queue] Application {thread}-> Session -> DtlsTransport -> SRTP -> RTP/RTCP
	// ICE에서는 STUN을 제외한 모든 패킷을 위로 올린다.
	// DTLS에서는 패킷을 받으면 DTLS인 경우 패킷을 버퍼에 쌓고(_dtls_packet_buffer) SSL_read를 호출하여 읽어서
	// 복호화를 한 후 다음 Layer로 전송한다.
	// DTLS가 아니고 RTP/RTCP인 경우는 SRTP로 보낸다.
	// 그 외에는 모르는 패킷이므로 처리하지 않는다.
	bool RecvPacket(const std::shared_ptr<ov::Data> &data);

protected:
	// SSL에서 암호화 할 패킷을 읽어갈 때 호출한다. _packet_buffer에 쌓인 패킷을 준다.
	ssize_t Read(ov::Tls *tls, void *buffer, size_t length);

	// SSL에서 복호화한 패킷의 전송을 요청할 때 호출한다. ice로 최종 전송한다.
	ssize_t Write(ov::Tls *tls, const void *data, size_t length);

	bool VerifyPeerCertificate();

private:
	bool ContinueSSL();
	bool IsDtlsPacket(const std::shared_ptr<const ov::Data> data);
	bool IsRtpPacket(const std::shared_ptr<const ov::Data> data);
	bool SaveDtlsPacket(const std::shared_ptr<const ov::Data> data);
	std::shared_ptr<const ov::Data> TakeDtlsPacket();

	bool MakeSrtpKey();

	enum SSLState
	{
		SSL_NONE,
		SSL_WAIT,
		SSL_CONNECTING,
		SSL_CONNECTED,
		SSL_ERROR,
		SSL_CLOSED
	};

	SSLState _state;
	bool _peer_certificate_verified;
	std::shared_ptr<info::Session> _session_info;
	std::shared_ptr<IcePort> _ice_port;
	std::shared_ptr<SrtpTransport> _srtp_transport;
	std::shared_ptr<::Certificate> _local_certificate;
	std::shared_ptr<ov::TlsContext> _tls_context;
	std::shared_ptr<::Certificate> _peer_certificate;
	ov::String _peer_fingerprint_algorithm;
	ov::String _peer_fingerprint_value;

	// SSL이 가져갈 패킷을 임시로 보관하는 버퍼, 동시에 1개만 저장한다.
	std::deque<std::shared_ptr<const ov::Data>> _packet_buffer;

	// SSL *_ssl;
	// SSL_CTX *_ssl_ctx;

	std::mutex _tls_lock;

	ov::Tls _tls;
};
