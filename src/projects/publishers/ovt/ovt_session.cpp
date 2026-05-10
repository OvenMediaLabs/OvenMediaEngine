
#include <base/info/stream.h>
#include <base/ovlibrary/byte_io.h>
#include <base/publisher/stream.h>
#include <modules/ovt_packetizer/ovt_packet.h>
#include <monitoring/monitoring.h>
#include "ovt_session.h"
#include "ovt_private.h"

namespace
{
	bool ExtractMediaPacketTrackId(const std::shared_ptr<OvtPacket> &packet, uint32_t &track_id)
	{
		if ((packet == nullptr) || (packet->PayloadType() != OVT_PAYLOAD_TYPE_MEDIA_PACKET))
		{
			return false;
		}

		if ((packet->PayloadLength() < sizeof(uint32_t)) || (packet->Payload() == nullptr))
		{
			return false;
		}

		track_id = ByteReader<uint32_t>::ReadBigEndian(packet->Payload());
		return true;
	}
}  // namespace

std::shared_ptr<OvtSession> OvtSession::Create(const std::shared_ptr<pub::Application> &application,
											   const std::shared_ptr<pub::Stream> &stream,
											   uint32_t session_id,
											   const std::shared_ptr<ov::Socket> &connector,
											   const std::optional<std::set<int32_t>> &allowed_track_ids)
{
	auto session_info = info::Session(*std::static_pointer_cast<info::Stream>(stream), session_id);
	auto session = std::make_shared<OvtSession>(session_info, application, stream, connector, allowed_track_ids);
	if(!session->Start())
	{
		return nullptr;
	}
	return session;
}

OvtSession::OvtSession(const info::Session &session_info,
					   const std::shared_ptr<pub::Application> &application,
					   const std::shared_ptr<pub::Stream> &stream,
					   const std::shared_ptr<ov::Socket> &connector,
					   const std::optional<std::set<int32_t>> &allowed_track_ids)
	: pub::Session(session_info, application, stream)
{
	_connector = connector;
	_sent_ready						 = false;
	_allowed_track_ids				 = allowed_track_ids;
	_current_media_packet_allowed	 = true;
	_has_current_media_packet_filter = false;

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
	if (_connector != nullptr)
	{
		_connector->Close();
	}

	return Session::Stop();
}

bool OvtSession::UpdateAllowedTrackIds(const std::optional<std::set<int32_t>> &allowed_track_ids)
{
	std::lock_guard<std::mutex> lock(_filter_lock);

	// Avoid the resync-to-next-marker freeze when the new scope is at least as wide as the
	// current scope. In that case every previously allowed packet is still allowed, so an
	// in-flight fragment chain can finish under the existing decision and the next fragment
	// will be re-evaluated against the new filter at its first packet (which carries the
	// track id).  Narrowing or unrelated scopes still need the marker-aligned resync because
	// previously allowed mid-fragment packets may belong to a track we now have to drop.
	const bool widening_or_unchanged =
		(allowed_track_ids.has_value() == false) ||
		(_allowed_track_ids.has_value() &&
		 std::includes(allowed_track_ids->begin(), allowed_track_ids->end(),
					   _allowed_track_ids->begin(), _allowed_track_ids->end()));

	_allowed_track_ids = allowed_track_ids;

	if (widening_or_unchanged == false)
	{
		_current_media_packet_allowed	 = true;
		_has_current_media_packet_filter = false;
		_current_media_packet_track_id.reset();
		_sent_ready = false;
		return true;
	}

	// Widening (or unchanged): re-evaluate the in-flight fragment under the new
	// filter using the track id cached on its first packet. A fragment dropped
	// under the old filter starts flowing immediately if widening just allowed
	// its track. If the cache is empty (track id wasn't extractable on the first
	// packet), keep the prior decision - mid-fragment packets carry no track id.
	if (_has_current_media_packet_filter && _current_media_packet_track_id.has_value())
	{
		_current_media_packet_allowed = IsTrackAllowedLocked(*_current_media_packet_track_id);
	}
	return true;
}

std::optional<std::set<int32_t>> OvtSession::GetAllowedTrackIds() const
{
	std::lock_guard<std::mutex> lock(_filter_lock);
	return _allowed_track_ids;
}

bool OvtSession::IsTrackAllowedLocked(uint32_t track_id) const
{
	if (_allowed_track_ids.has_value() == false)
	{
		return true;
	}

	return _allowed_track_ids->find(static_cast<int32_t>(track_id)) != _allowed_track_ids->end();
}

bool OvtSession::BeginMediaPacketFilterDecisionLocked(const std::shared_ptr<OvtPacket> &packet)
{
	if (_allowed_track_ids.has_value() == false)
	{
		_current_media_packet_allowed	 = true;
		_has_current_media_packet_filter = true;
		_current_media_packet_track_id.reset();
		return true;
	}

	uint32_t track_id = 0;
	if (ExtractMediaPacketTrackId(packet, track_id) == false)
	{
		logtw("OvtSession(%d) could not resolve media track id from OVT packet payload", GetId());
		_current_media_packet_allowed	 = false;
		_has_current_media_packet_filter = true;
		_current_media_packet_track_id.reset();
		return false;
	}

	_current_media_packet_track_id	 = track_id;
	_current_media_packet_allowed	 = IsTrackAllowedLocked(track_id);
	_has_current_media_packet_filter = true;

	return _current_media_packet_allowed;
}

bool OvtSession::ShouldForwardPacketLocked(const std::shared_ptr<OvtPacket> &packet)
{
	if (packet == nullptr)
	{
		return false;
	}

	if (packet->PayloadType() != OVT_PAYLOAD_TYPE_MEDIA_PACKET)
	{
		return true;
	}

	if (_allowed_track_ids.has_value() == false)
	{
		return true;
	}

	if (_has_current_media_packet_filter == false)
	{
		BeginMediaPacketFilterDecisionLocked(packet);
	}

	auto should_forward = _current_media_packet_allowed;

	if (packet->Marker())
	{
		_has_current_media_packet_filter = false;
		_current_media_packet_allowed	 = true;
		_current_media_packet_track_id.reset();
	}

	return should_forward;
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
	{
		std::lock_guard<std::mutex> lock(_filter_lock);
		if (_sent_ready == false)
		{
			if (session_packet->Marker())  // Set marker
			{
				_sent_ready = true;
			}

			return;
		}

		if (ShouldForwardPacketLocked(session_packet) == false)
		{
			return;
		}
	}

	// Set OVT Session ID into packet
	auto copy_packet = std::make_shared<OvtPacket>(*session_packet);
	copy_packet->SetSessionId(GetId());

	EmitPacket(copy_packet);
}

bool OvtSession::EmitPacket(const std::shared_ptr<OvtPacket> &packet)
{
	if ((_connector == nullptr) || (packet == nullptr))
	{
		return false;
	}

	return _connector->Send(packet->GetData());
}

const std::shared_ptr<ov::Socket> OvtSession::GetConnector()
{
	return _connector;
}

void OvtSession::OnMessageReceived(const std::any &message)
{
	// NOTHING YET
}
