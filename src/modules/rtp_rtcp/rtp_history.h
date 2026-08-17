#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "rtx_rtp_packet.h"

// WebRTC-Native-Code uses 9600 value
#define DEFAULT_MAX_HISTORY_CAPACITY	1500
// Stored RTP packet is only valid for 3 second after being created
#define VALID_TIME_MS_STORED_RTP_PACKET	3000

class RtpHistory
{
public:
	RtpHistory(uint8_t origin_payload_type, uint8_t rtx_payload_type, uint32_t rtx_ssrc, uint32_t max_history_size = DEFAULT_MAX_HISTORY_CAPACITY);

	// Converting to RtxRtpPacket
	bool StoreRtpPacket(const std::shared_ptr<RtpPacket> &packet);
	std::shared_ptr<RtxRtpPacket> GetRtxRtpPacket(uint16_t seq_no);

	uint8_t	GetOriginPayloadType();
	uint32_t GetRtxSsrc();
	uint8_t GetRtxPayloadType();

private:
	uint16_t GetIndex(uint16_t seq_no);
	
	// This map maps a hash(made with "origin sequence number" % max_history_size) and an RtpPacket.
	// And this map never erases items for performance reason. 

	// Yes, this map will have a collision once per max_history_size. 
	// However, the default value of 9600 is a very large value, and collision packets 
	// with 9600 sequence number differences are very old packets. 
	// Since RTP packets have a creation time, old packets may not be used. 
	// This can avoid collision. Therefore, set max_history_size to a large value as possible.
	std::shared_mutex	_history_lock;
	std::unordered_map<uint16_t, std::shared_ptr<RtpPacket>> _history;

	// Creating RtxRtpPacket requires computing resources, but not all of them are used
	// (only for packets requested by the session with NACK). 
	// Therefore, it is unnecessary to create all packets with RtxRtpPacket in advance. 
	// It is also wasteful to create another RtxRtpPacket once created. 
	// When GetRtxRtpPacket() is called, it first looks for a packet in the cache and 
	// checks if it is valid (by creation time). 
	// If it is not valid, a new RtxRtpPacket is created, cached, and returned.
	std::shared_mutex	_history_cache_lock;
	std::unordered_map<uint16_t, std::shared_ptr<RtxRtpPacket>> _history_cache;

	uint8_t		_origin_paylod_type;
	uint32_t	_rtx_ssrc;
	uint8_t		_rtx_paylod_type;
	uint32_t	_max_history_size;
};