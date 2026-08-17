//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <netinet/in.h>

enum class StunClass : uint8_t
{
	Request = 0b00,
	Indication = 0b01,
	SuccessResponse = 0b10,
	ErrorResponse = 0b11,
};

enum class StunMethod : uint16_t
{
	Binding = 0x001,
	// Reserved = 0x002 (was SharedSecret prior to [RFC5389])
	
	//////////////////
	// for TURN : https://www.rfc-editor.org/rfc/rfc8656.html#name-stun-methods
	//////////////////
	Allocate = 0x003,
	Refresh = 0x004,
	Send = 0x006,
	Data = 0x007,
	CreatePermission = 0x008,
	ChannelBind = 0x009
};

enum class StunAttributeType : uint16_t
{
	// 15.1. MAPPED-ADDRESS
	MappedAddress = 0x0001,
	// 15.2. XOR-MAPPED-ADDRESS
	XorMappedAddress = 0x0020,
	// 15.3. USERNAME
	UserName = 0x0006,
	// 15.4. MESSAGE-INTEGRITY
	MessageIntegrity = 0x0008,
	// 15.5. FINGERPRINT
	Fingerprint = 0x8028,
	// 15.6. ERROR-CODE
	ErrorCode = 0x0009,
	// 15.7. REALM
	Realm = 0x0014,
	// 15.8. NONCE
	Nonce = 0x0015,
	// 15.9. UNKNOWN-ATTRIBUTES
	UnknownAttributes = 0x000A,
	// 15.10. SOFTWARE
	Software = 0x8022,
	// 15.11. ALTERNATE-SERVER
	AlternateServer = 0x8023,

	/////////////////
	// ICE : RFC8445 , 20.1.  STUN Attributes
	/////////////////
	Priority = 0x0024,
	UseCandidate = 0x0025,
	IceControlled = 0x8029,
	IceControlling = 0x802A,

	//////////////
	// Turn : https://www.rfc-editor.org/rfc/rfc8656.html#section-18
	//////////////
	ChannelNumber = 0x000C,
	Lifetime = 0x000D,
	// Reserved = 0x0010, // (was BANDWIDTH)
	XorPeerAddress = 0x0012,
	Data = 0x0013,
	XorRelayedAddress = 0x0016,
	RequestedAddressFamily = 0x0017,
	EvenPort = 0x0018,
	RequestedTransport = 0x0019,
	DontFragment = 0x001A,
	// Reserved  = 0x0021, (was TIMER-VAL)
	ReservationToken = 0x0022,
	AdditionalAddressFamily = 0x8000,
	AddressErrorCode = 0x8001,
	ICMP = 0x8004
};

// RFC5389, 15.2. XOR-MAPPED-ADDRESS
enum class StunAddressFamily : uint8_t
{
	Unknown = 0x00,

	IPv4 = 0x01,
	IPv6 = 0x02
};

// RFC5389, 15.6. ERROR-CODE
enum class StunErrorCode : uint16_t
{
	TryAlternate = 300,
	BadRequest = 400,
	Unauthonticated = 401,
	Forbidden = 403,
	UnknownAttribute = 420,
	AllocationMismatch = 437,
	StaleNonce = 438,
	AddressFamilyNotSupported = 440,
	WrongCredentials = 441,
	UnsupportedTransportProtocol = 442,
	PeerAddressFamilyMismatch = 443,
	AllocationQuotaReached = 486,
	RoleConflict = 487, // ICE, RFC8445
	ServerError = 500,
	InsufficientCapacity = 508
};

// TODO: legacy 버전(RFC3489)에서는 길이가 다름. RFC3489는 나중에 추가할 것
// Unit: bytes
#define OV_STUN_TRANSACTION_ID_LENGTH                           12

#define OV_STUN_FINGERPRINT_XOR_VALUE                           0x5354554E
#define OV_STUN_MAGIC_COOKIE                                    0x2112A442
// SHA1 digest 크기와 동일
#define OV_STUN_HASH_LENGTH                                     20
