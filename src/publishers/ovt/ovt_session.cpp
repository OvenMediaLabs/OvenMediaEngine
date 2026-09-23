#include <base/info/stream.h>
#include <base/ovlibrary/byte_io.h>
#include <base/publisher/stream.h>
#include <modules/ovt_packetizer/ovt_packet.h>
#include <modules/ovt_packetizer/ovt_packetizer.h>
#include <modules/ovt_packetizer/ovt_wire.h>
#include <monitoring/monitoring.h>
#include "ovt_session.h"
#include "ovt_stream.h"
#include "ovt_private.h"

std::shared_ptr<OvtSession> OvtSession::Create(const std::shared_ptr<pub::Application> &application,
										  	   const std::shared_ptr<pub::Stream> &stream,
										  	   uint32_t session_id,
										  	   const std::shared_ptr<ov::Socket> &connector)
{
	auto session_info = info::Session(*std::static_pointer_cast<info::Stream>(stream), session_id);
	auto session = std::make_shared<OvtSession>(session_info, application, stream, connector);
	if(!session->Start())
	{
		return nullptr;
	}
	return session;
}

OvtSession::OvtSession(const info::Session &session_info,
		   const std::shared_ptr<pub::Application> &application,
		   const std::shared_ptr<pub::Stream> &stream,
		   const std::shared_ptr<ov::Socket> &connector)
   : pub::Session(session_info, application, stream)
{
	_connector = connector;
	_sent_ready = false;

	MonitorInstance->OnSessionConnected(*GetStream(), PublisherType::Ovt);
}

OvtSession::~OvtSession()
{
	Stop();
	logtt("OvtSession(%d) has been terminated finally", GetId());

	MonitorInstance->OnSessionDisconnected(*GetStream(), PublisherType::Ovt);
}

bool OvtSession::Start()
{
	logtt("OvtSession(%d) has started", GetId());
	return Session::Start();
}

bool OvtSession::Stop()
{
	logtt("OvtSession(%d) has stopped", GetId());
	_connector->Close();
	
	return Session::Stop();
}

bool OvtSession::SendMessageDirect(OvtPayloadType payload_type, const ov::String &payload)
{
	if (_connector == nullptr)
	{
		return false;
	}

	OvtPacketizer packetizer;
	if (packetizer.PacketizeMessage(payload_type, ov::Clock::NowMSec(), payload.ToData(false)) == false)
	{
		return false;
	}

	// Every fragment goes out in one Send() even when the message needs several.
	// The stream worker is not the only writer on this socket: the socket worker answers requests
	// on it, and an edge appends consecutive message fragments into one buffer,
	// so a response landing between two fragments would be glued onto this message.
	auto data = std::make_shared<ov::Data>();
	while (packetizer.IsAvailablePackets())
	{
		auto packet = packetizer.PopPacket();
		if (packet == nullptr)
		{
			return false;
		}

		packet->SetSessionId(GetId());
		data->Append(packet->GetData());
	}

	if (data->GetLength() == 0)
	{
		return false;
	}

	return _connector->Send(data);
}

void OvtSession::SendTrackSnapshotIfStale()
{
	if (_track_epoch_cursor.IsSettled())
	{
		return;
	}

	// An OVT1 edge reads neither the snapshot nor the OVT2 fields it carries.
	// Its describe went out in the OVT2 form as well, and it ignored the keys it does not know,
	// so there is nothing to catch it up with either way.
	if (_is_ovt2 == false)
	{
		_track_epoch_cursor.MarkSettled();
		return;
	}

	auto stream = std::static_pointer_cast<OvtStream>(GetStream());
	if (stream == nullptr)
	{
		return;
	}

	// One atomic read, and only until the first delivery settles this
	auto epoch = stream->GetTrackEpoch();
	if (_track_epoch_cursor.NeedsSnapshot(epoch) == false)
	{
		// Nothing changed while this session was behind the gate, which is the common case
		_track_epoch_cursor.MarkSettled();
		return;
	}

	// The snapshot says the same thing a describe would, so it carries the tracks this session
	// subscribed to and no others.
	auto tracks = stream->GetTracks();
	{
		ov::ScopedLock lock(_track_set_filter_mutex);

		if (_track_set_filter_enabled)
		{
			for (auto it = tracks.begin(); it != tracks.end();)
			{
				it = (_allowed_track_ids.count(static_cast<uint32_t>(it->first)) == 0) ? tracks.erase(it) : std::next(it);
			}
		}
	}

	auto snapshot = OvtStream::BuildTrackSnapshot(tracks);
	if (snapshot.IsEmpty())
	{
		_track_epoch_cursor.MarkSettled();
		return;
	}

	if (SendMessageDirect(OvtPayloadType::MessageResponse, snapshot) == false)
	{
		// Left unsettled so the next packet tries again
		return;
	}

	logti("OvtSession(%d) was sent every track's configuration before its first media packet (stream track epoch %u)",
		  GetId(), epoch);

	_track_epoch_cursor.MarkSettled();
}

void OvtSession::SendRequiredIfChanged()
{
	auto stream = std::static_pointer_cast<OvtStream>(GetStream());
	if (stream == nullptr)
	{
		return;
	}

	// One atomic read on the media path; the lock is taken only when the version moved
	if (_required_cursor.NeedsSend(stream->GetRequiredVersion()) == false)
	{
		return;
	}

	auto snapshot = stream->GetRequiredSnapshot();

	// Nothing to announce. An empty set can never make an edge refuse anything, so it is not sent.
	// The version is recorded all the same, so a stream that never adds a token stops at the atomic
	// read above instead of taking the lock on every media packet.
	if (snapshot.tokens->empty())
	{
		_required_cursor.MarkSent(snapshot.version);
		return;
	}

	if (SendMessageDirect(OvtPayloadType::Required, ovt::MakeRequiredPayload(*snapshot.tokens)))
	{
		_required_cursor.MarkSent(snapshot.version);
	}
}

void OvtSession::SendOutgoingData(const std::any &packet)
{
	std::shared_ptr<OvtPacket> session_packet;

	try 
	{
        session_packet = std::any_cast<std::shared_ptr<OvtPacket>>(packet);
		if(session_packet == nullptr)
		{
			return;
		}
    }
    catch(const std::bad_any_cast& e) 
	{
        logtt("An incorrect type of packet was input from the stream. (%s)", e.what());
		return;
    }

	// OvtSession should send full packet so it will start to send from next packet of marker packet.
	if(_sent_ready == false)
	{
		if(session_packet->Marker() == true) // Set marker
		{
			_sent_ready = true;
		}

		return;
	}

	// A NOTIFY broadcast while the gate was discarding packets never reached this session.
	// Only the first delivery does any work here; the calls after it return on `IsSettled()`.
	SendTrackSnapshotIfStale();

	// PT 40 goes out ahead of the first fragment of a media packet
	// whenever the stream's required set differs from the last one this session delivered
	// (or none was delivered yet). An OVT1 session never gets one.
	// Comparing here, on the single sending thread, keeps it ordered before that media.
	const auto is_media = ovt::CompareWireValue(session_packet->PayloadType(), OvtPayloadType::MediaPacket);
	if (is_media && _at_group_start && _is_ovt2)
	{
		SendRequiredIfChanged();
	}
	if (is_media)
	{
		_at_group_start = session_packet->Marker();
	}

	// TrackSet filter, applied to media packets only.
	// Broadcast signaling (the track change notification) passes through here unfiltered,
	// and the required set (PT 40) is written above.
	// The track id is read from the first 4 bytes of the payload
	{
		ov::ScopedLock lock(_track_set_filter_mutex);

		if (_track_set_filter_enabled && is_media)
		{
			if (_current_group_decision == GroupDecision::Pending)
			{
				if (session_packet->PayloadLength() >= 4)
				{
					uint32_t track_id = ByteReader<uint32_t>::ReadBigEndian(session_packet->Payload());
					if (_allowed_track_ids.find(track_id) != _allowed_track_ids.end())
					{
						_current_group_decision = GroupDecision::Accept;
					}
					else
					{
						_current_group_decision = GroupDecision::Drop;
					}
				}
				else
				{
					// Malformed first fragment; drop conservatively.
					if (_warned_malformed_first_fragment == false)
					{
						_warned_malformed_first_fragment = true;
						logtw(
							"OvtSession(%u) received a malformed first fragment from %s "
							"(payload_length=%u, under the 4 bytes the track id needs); dropping. "
							"Further occurrences will be suppressed.",
							GetId(),
							_connector != nullptr ? _connector->ToString().CStr() : "<unknown>",
							session_packet->PayloadLength());
					}
					_current_group_decision = GroupDecision::Drop;
				}
			}

			bool drop = (_current_group_decision == GroupDecision::Drop);

			if (session_packet->Marker() == true)
			{
				_current_group_decision = GroupDecision::Pending;
			}

			if (drop)
			{
				// A dropped group leaves nothing half-assembled behind
				_unit_buffer.reset();
				return;
			}
		}
	}

	// Every packet a session writes carries its id; the edge does not read it.
	// The field is reserved for multiplexing sessions over one connection,
	// and is not used for anything else.
	auto copy_packet = std::make_shared<OvtPacket>(*session_packet);
	copy_packet->SetSessionId(GetId());

	// One logical unit goes out in one `Send()`, the rule `SendMessageDirect()` and
	// `OvtPublisher::SendResponse()` already follow. An edge reassembles a message or a media packet
	// from consecutive fragments, so anything another writer on this connection puts between two of
	// them lands in the same reassembly buffer. The packetizer marks the last fragment of every unit,
	// so this holds nothing past the unit being assembled.
	if (((_unit_buffer == nullptr) || _unit_buffer->IsEmpty()) && session_packet->Marker())
	{
		// A unit that fits in one packet, which is every message and every small media packet
		_connector->Send(copy_packet->GetData());
		return;
	}

	if (_unit_buffer == nullptr)
	{
		_unit_buffer = std::make_shared<ov::Data>();
	}

	_unit_buffer->Append(copy_packet->GetData());

	if (session_packet->Marker() == false)
	{
		return;
	}

	auto unit = _unit_buffer;
	_unit_buffer.reset();

	_connector->Send(unit);
}

void OvtSession::SetAllowedTrackIds(const std::set<uint32_t> &allowed_track_ids)
{
	ov::ScopedLock lock(_track_set_filter_mutex);

	_allowed_track_ids		  = allowed_track_ids;
	_track_set_filter_enabled = true;
	_current_group_decision	  = GroupDecision::Pending;
}

const std::shared_ptr<ov::Socket> OvtSession::GetConnector()
{
	return _connector;
}

void OvtSession::OnMessageReceived(const std::any &message)
{
	// NOTHING YET
}