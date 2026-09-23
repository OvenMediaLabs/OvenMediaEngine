//
// Created by getroot on 19. 12. 12.
//
#pragma once

#include <base/mediarouter/media_buffer.h>

#include <optional>

#include "ovt_packet.h"
#include "ovt_packetizer_interface.h"

#define INIT_PACKET_BUFFER_SIZE 65535
#define INIT_PAYLOAD_BUFFER_SIZE 1024 * 1024  // 1MB

// A message or media packet whose fragments exceed this before the marker arrives fails the connection,
// so an endless unmarked stream cannot grow the reassembly buffer.
#define MAX_MESSAGE_BUFFER_SIZE (16 * 1024 * 1024)
#define MAX_MEDIA_PACKET_BUFFER_SIZE (64 * 1024 * 1024)

class OvtDepacketizer
{
public:
	// One reassembled message with the payload type it arrived on.
	// Consumers decide by the type: the origin only takes `MessageRequest`,
	// and the edge's synchronous waits only take `MessageResponse`.
	struct Message
	{
		OvtPayloadType payload_type;
		std::shared_ptr<ov::Data> data;
	};

public:
	// `MediaRole::MessagesOnly` is for the read side of an OVT publisher. That direction carries
	// requests and nothing else: OVT is a pull protocol, so media travels the other way over the same
	// connection. This holds at any relay depth, because it is the direction that decides it, not the
	// role of the peer. A media packet arriving there is discarded per fragment instead of being
	// reassembled first, so a peer cannot make an origin hold a payload it is certain to throw away.
	enum class MediaRole : uint8_t
	{
		MessagesAndMedia,
		MessagesOnly,
	};

	explicit OvtDepacketizer(MediaRole media_role = MediaRole::MessagesAndMedia);
	~OvtDepacketizer();

	bool AppendPacket(const void *data, size_t length);
	bool AppendPacket(const std::shared_ptr<const ov::Data> &packet);

	// Messages and media share one queue in on-wire order, so these answer the same question
	// about the item at the front of it: a consumer that must preserve their relative order
	// (e.g. a track-change signaling message that has to apply before the media that follows it)
	// asks before popping either kind.
	bool IsAvailableMessage();
	bool IsAvailableMediaPacket();
	std::optional<Message> PopMessage();
	const std::shared_ptr<MediaPacket> PopMediaPacket();

	// Whether the queue holds anything at all, of either kind
	bool IsAvailable();
	bool IsNextMessage();

	// Media fragments discarded because this instance takes messages only.
	// Nothing reaches the queue in that mode, so a consumer cannot see them any other way.
	size_t GetDroppedMediaFragmentCount() const
	{
		return _dropped_media_fragments;
	}

private:
	bool ParsePacket();
	bool AppendMessagePacket(OvtPayloadType payload_type, const std::shared_ptr<OvtPacket> &packet);
	bool AppendMediaPacket(const std::shared_ptr<OvtPacket> &packet);

	// One completed item.
	// Messages and media share a single queue so their relative on-wire order is never lost,
	// and popping the wrong type is a guarded no-op instead of a silent desynchronization.
	// `media_packet` is set only for `MediaPacket`.
	struct Item
	{
		OvtPayloadType payload_type;
		std::shared_ptr<ov::Data> message;
		std::shared_ptr<MediaPacket> media_packet;
	};

	const MediaRole _media_role;
	size_t _dropped_media_fragments = 0;

	std::shared_ptr<ov::Data> _packet_buffer;

	ov::Data _message_buffer;
	// Payload type of the message being reassembled; a fragment of another type is a broken stream
	std::optional<OvtPayloadType> _message_payload_type;
	ov::Data _media_packet_buffer;

	// Completed items in on-wire parse order; front is the next item on the wire.
	std::queue<Item> _items;
};
