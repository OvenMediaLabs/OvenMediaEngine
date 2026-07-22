//
// Created by getroot on 19. 12. 12.
//
#pragma once

#include <base/mediarouter/media_buffer.h>
#include "ovt_packet.h"
#include "ovt_packetizer_interface.h"

#define INIT_PACKET_BUFFER_SIZE		65535
#define INIT_PAYLOAD_BUFFER_SIZE	1024 * 1024 		// 1MB

class OvtDepacketizer
{
public:
	OvtDepacketizer();
	~OvtDepacketizer();

	bool AppendPacket(const void *data, size_t length);
	bool AppendPacket(const std::shared_ptr<const ov::Data> &packet);

	bool IsAvailableMessage();
	bool IsAvailableMediaPacket();
	const std::shared_ptr<ov::Data> PopMessage();
	const std::shared_ptr<MediaPacket> PopMediaPacket();

	// Messages and media share a single on-wire parse order. A consumer that must
	// preserve their relative order (e.g. a track-change signaling message that has
	// to apply before the media that follows it) pops via IsAvailable()/IsNextMessage()
	// instead of draining the two queues separately.
	bool IsAvailable();
	bool IsNextMessage();

private:
	bool ParsePacket();
	bool AppendMessagePacket(const std::shared_ptr<OvtPacket> &packet);
	bool AppendMediaPacket(const std::shared_ptr<OvtPacket> &packet);

	enum class ItemType : uint8_t
	{
		Message,
		MediaPacket
	};

	std::shared_ptr<ov::Data>					_packet_buffer;

	ov::Data									_message_buffer;
	ov::Data									_media_packet_buffer;

	std::queue<std::shared_ptr<ov::Data>>		_messages;
	std::queue<std::shared_ptr<MediaPacket>>	_media_packets;
	// Type of each completed item in parse order; front is the next item on the wire.
	std::queue<ItemType>						_order;
};
