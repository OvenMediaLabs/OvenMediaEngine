//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>
#include <memory>
#include <base/common_types.h>
#include <base/ovlibrary/ovlibrary.h>

// V (2 bits) names the OVT header layout; 1 is the layout below.
// A value, once used, is reserved for good and never reused,
// because an old peer would read the new layout as the old one.
// The first layout with V != 1 must carry its own extension field
// (the Reserved bits or a version byte after OVT[0]),
// so that the two remaining values do not become the ceiling.
// Its fixed header must be at least 18 bytes:
// an OVT1 peer does not parse before 18 bytes have arrived,
// so a shorter header would only be refused later.
// Reserved (5 bits) is sent as 0 and never read,
// so packet-level flags can be added here without touching V.
// Structured extensions go into a payload type instead.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=1|M|Reserved-| Payload Type  |       Sequence Number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           Timestamp                           |
// |                              ...                              |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |             			   Session ID    	                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           Payload Length      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// [SessionID] - Reserved
// Designed for the purpose of classifying each session when two or more sessions are connected with the same 5tuple in the future. Currently not used because the client connects to the server using a different port.
// It is kept for that multiplexing use and for nothing else.
// Every response carries 0: `OvtPublisher::SendResponse()` takes a session id and does not write it,
// and its local packetizer leaves the field at 0.
// Only what a session writes itself (media, notifications, the required set) carries an id,
// and the edge does not read that either.

/***********************************************
 * Protocol Specification
 ***********************************************
 OvenTransport is a protocol for Origin-Edge of OvenMediaEngine.
 This is a state protocol and performs both signaling and data transmission with one port.
 Therefore, the connection must be maintained.
 If the Session is disconnected, OVT determines in the same manner as the STOP command.

 [1] DESCRIBE
 <C->S>
 	M  : 0 or 1(Last packet)
	PT : MESSAGE REQUEST(10)
    SI : 0		
 	SN : 0
 	TS : Unix timestamp
 	Payload :
 		{
 			"id": 3921931,
			"application" : "describe",
 			"target": "ovt://host:port/app/stream"
 		}
 <S->C>
 	M  : 0 or 1(Last packet)
 	PT : MESSAGE RESPONSE(20)
 	SI : 0
 	SN : 0
 	TS : Unix timestamp
 	Payload :
 		{
 			"id": 3921931,
			"application" : "describe",
			"code" : 200 | 404 | 500,
			"message" : "ok" | "app/stream not found" | "Internal Server Error",
			"contents" :
			{
				"stream" :
				{
					"appName" : "app",
					"streamName" : "stream_720p",
					"tracks":
					[
						{
							"id" : 3291291,
							"codecId" : 32198392,
							"mediaType" : 0 //0:"video" | 1:"audio" | 2:"data",
							"timebase_num" : 90000,
							"timebase_den" : 90000,
							"bitrate" : 5000000,
							"startFrameTime" : 1293219321,
							"lastFrameTime" : 1932193921,
							"videoTrack" :
							{
								"framerate" : 29.97,
								"width" : 1280,
								"height" : 720
							},
							"audioTrack" :
							{
								"samplerate" : 44100,
								"sampleFormat" : "s16",
								"layout" : "stereo"
							}
						}
					]
				}
			}
		}

 [1] PLAY, STOP
 <C->S>
 	M  : 0 or 1(Last packet)
 	PT : MESSAGE REQUEST(10)
 	SI : 0
 	SN : 0
 	TS : Unix timestamp
 	Payload :
 		{
 			"id": 3921932,
			"application" : "play", "stop",
 			"target": "ovt://host:port/app/stream"
 		}

 		<! Later version can be extended to specify tracks or add other options. >

 <S->C>
 	M  : 0 or 1(Last packet)
 	PT : MESSAGE RESPONSE(20)
 	SI : 11992
 	SN : 0
 	TS : Unix timestamp
 	Payload :
		{
			"id": 3921932,
			"application" : "play" | "stop",
			"code" : 200 | 404 | 500,
			"message" : "ok" | "app/stream not found" | "Internal Server Error",
		}

		while(STOP or DISCONNECTED)
		{
			M  : 0 or 1
			PT : MEDIA (30)
			SI : 11992
			SN : 1 ~ rolling
			TS : Unix timestamp
			Payload :
			[Binary - Serialized MediaPacket]
		}

 **********************************************/

// V field of the current header layout. See the notes above the header diagram before changing it.
#define OVT_VERSION 1
#define OVT_FIXED_HEADER_SIZE 18
#define OVT_DEFAULT_MAX_PACKET_SIZE			32768
#define OVT_DEFAULT_MAX_PAYLOAD_SIZE		OVT_DEFAULT_MAX_PACKET_SIZE - OVT_FIXED_HEADER_SIZE;

// Every protocol value is written out explicitly;
// none of these may rely on the implicit enum numbering.
// 0 is never assigned. Nothing in the code depends on that any more, but every release up to
// v0.21.0.0 read it as "no header", so the value stays reserved.
enum class OvtPayloadType : uint8_t
{
	MessageRequest	= 10,  // Edge -> Origin request
	MessageResponse = 20,  // Response to a request, and Origin-initiated messages (notify, stop)
	MediaPacket		= 30,  // Serialized MediaPacket
	Required		= 40,  // Origin -> Edge: the required tokens a session must know before its next media packet
};

// Using MediaPacket (De)Packetizer
#define MEDIA_PACKET_HEADER_SIZE			(32+64+64+64+8+8+8+8+32)/8

class OvtPacket
{
public:
	OvtPacket();
	OvtPacket(OvtPacket &src);
	OvtPacket(const ov::Data &data);
	virtual ~OvtPacket();

	bool 		LoadHeader(const ov::Data &data);
	bool 		Load(const ov::Data &data);

	bool		IsHeaderAvailable() const;
	bool 		IsPacketAvailable() const;

	uint8_t 	Version() const;
	bool 		Marker() const;
	uint8_t 	PayloadType() const;
	uint16_t 	SequenceNumber() const;
	uint64_t 	Timestamp() const;
	uint32_t 	SessionId() const;
	uint32_t	PacketLength() const;
	uint16_t 	PayloadLength() const;
	const uint8_t*	Payload() const;

	void 		SetMarker(bool marker_bit);
	void 		SetPayloadType(OvtPayloadType payload_type);
	void 		SetSequenceNumber(uint16_t sequence_number);
	void 		SetTimestampNow();
	void 		SetTimestamp(uint64_t timestamp);
	void 		SetSessionId(uint32_t session_id);

	bool 		SetPayload(const uint8_t *payload, size_t payload_size);

	const uint8_t* GetBuffer() const;
	const std::shared_ptr<ov::Data>& GetData() const;
	size_t GetDataLength() const;

private:
	// Raw byte off the wire; `LoadHeader()` stores it as-is and the depacketizer maps it through `FromOvtWire()`
	void 		SetRawPayloadType(uint8_t payload_type);
	void 		SetPayloadLength(size_t payload_length);

	bool 		_is_packet_available = false;

	// Initialized here so that every constructor starts from a defined state:
	// `OvtPacket(const ov::Data &)` only calls `Load()`, which may reject the header
	// before it assigns anything.
	uint8_t		_version = OVT_VERSION;
	uint8_t 	_marker = 0;
	uint8_t 	_payload_type = 0;
	// Whether this object carries a header: `LoadHeader()` accepted one on the parse path,
	// or `SetPayloadType()` wrote one on the build path. `_payload_type` cannot answer this:
	// a peer that writes payload type 0 would make the state read as "no header".
	bool		_header_loaded = false;
	uint16_t 	_sequence_number = 0;
	uint64_t 	_timestamp = 0;
	uint32_t 	_session_id = 0;
	uint16_t 	_payload_length = 0;

	uint8_t *					_buffer = nullptr;
	std::shared_ptr<ov::Data>	_data;
};
