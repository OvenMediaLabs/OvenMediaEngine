//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>
#include <utility>
// ICE candidate structure:
// [{"candidate":"candidate:0 1 UDP 50 192.168.0.183 10000 typ host generation 0","sdpMLineIndex":0,"sdpMid":"video"}]

class IceCandidate
{
public:
	// RFC5245 - 15.1.  "candidate" Attribute
	// candidate-attribute   = "candidate" ":" foundation SP component-id SP
	//                         transport SP
	//                         priority SP
	//                         connection-address SP     ;from RFC 4566
	//                         port         ;port from RFC 4566
	//                         SP cand-type
	//                         [SP rel-addr]
	//                         [SP rel-port]
	//                         *(SP extension-att-name SP
	//                              extension-att-value)
	//
	// foundation            = 1*32ice-char
	// component-id          = 1*5DIGIT
	// transport             = "UDP" / transport-extension
	// transport-extension   = token              ; from RFC 3261
	// priority              = 1*10DIGIT
	// cand-type             = "typ" SP candidate-types
	// candidate-types       = "host" / "srflx" / "prflx" / "relay" / token
	// rel-addr              = "raddr" SP connection-address
	// rel-port              = "rport" SP port
	// extension-att-name    = byte-string    ;from RFC 4566
	// extension-att-value   = byte-string
	// ice-char              = ALPHA / DIGIT / "+" / "/"

	// 예:                 "candidate:0 1 UDP 50 192.168.0.183 10000 typ host generation 0"
	//                                v |  |   |       |         |    |   |        |     |
	//   foundation ----------------->~ v  |   |       |         |    |   |        |     |
	//   component-id ----------------->~  v   |       |         |    |   |        |     |
	//   transport ---------------------->~~~  v       |         |    |   |        |     |
	//   prioirty --------------------------->~~       v         |    |   |        |     |
	//   connection-address -------------------->~~~~~~~~~~~~~   v    |   |        |     |
	//   port ------------------------------------------------>~~~~~  v   |        |     |
	//   cand-type ------------------------------------------------->~~~  v        |     |
	//   candidate-types ----------------------------------------------->~~~~      v     |
	//   extension-att-name -------------------------------------------------->~~~~~~~~~ v
	//   extension-att-value ----------------------------------------------------------->~
	IceCandidate();
	IceCandidate(const ov::String &foundation, const ov::String &component_id, const ov::String &transport, uint32_t priority, const ov::String &cand_type, const ov::String &candidate_types, const ov::String &rel_addr, const ov::String &rel_port, const std::map<ov::String, ov::String> &extension_att) = delete;
	IceCandidate(const IceCandidate &candidate) = default;
	IceCandidate(const ov::String &transport, const ov::SocketAddress &address);
	IceCandidate(const ov::String &transport, const ov::String &address, int port);
	IceCandidate(IceCandidate &&candidate) noexcept;

	virtual ~IceCandidate();

	bool ParseFromString(const ov::String &candidate_string);

	IceCandidate &operator =(IceCandidate candidate) noexcept;
	bool operator <(const IceCandidate &candidate) const noexcept;

	const ov::String &GetFoundation() const noexcept;
	void SetFoundation(ov::String foundation);

	uint32_t GetComponentId() const;
	void SetComponentId(uint32_t component_id);

	const ov::String &GetTransport() const;
	void SetTransport(const ov::String &transport);

	uint32_t GetPriority() const;
	void SetPriority(uint32_t priority);

	ov::SocketAddress GetAddress() const;
	ov::String GetConnectionAddress() const;
	int GetPort() const;

	const ov::String &GetCandidateTypes() const;
	void SetCandidateTypes(const ov::String &candidate_types);

	const ov::String &GetRelAddr() const;
	void SetRelAddr(const ov::String &rel_addr);

	uint16_t GetRelPort() const;
	void SetRelPort(uint16_t rel_port);

	const std::map<ov::String, ov::String> &GetExtensionAttributes() const;
	void AddExtensionAttributes(const ov::String &key, const ov::String &value);
	bool RemoveExtensionAttributes(const ov::String &key);
	void RemoveAllExtensionAttributes();

	ov::String GetCandidateString() const noexcept;

	virtual ov::String ToString() const noexcept;

protected:

	void Swap(IceCandidate &from) noexcept;

	// 1*32ice-char
	ov::String _foundation;
	// 1*5DIGIT
	uint32_t _component_id;

protected:
	// ["UDP" | <transport-extension>] (RFC3261)
	ov::String _transport;
	// 1*10DIGIT
	uint32_t _priority;
	// <connection-address> <port> (RFC4566)
	ov::String _address_str;
	int _port;
	ov::SocketAddress _address;
	// "typ" ["host" | "srflx" | "prflx" | "relay" | token]
	ov::String _candidate_types;
	// "raddr" <connection-address>
	ov::String _rel_addr;
	// "rport" <port>
	uint16_t _rel_port;

	std::map<ov::String, ov::String> _extension_attributes;
};

