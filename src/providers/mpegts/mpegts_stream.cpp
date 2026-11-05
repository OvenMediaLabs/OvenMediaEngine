//==============================================================================
//
//  MpegTs Stream
//
//  Created by Hyunjun Jang
//  Moved by Getroot
//
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//==============================================================================

#include "mpegts_stream.h"

#include <base/info/media_extradata.h>
#include <base/mediarouter/media_type.h>
#include <orchestrator/orchestrator.h>

#include "base/info/application.h"
#include "base/provider/push_provider/application.h"
#include "modules/bitstream/aac/aac_adts_splitter.h"
#include "modules/bitstream/h265/h265_parser.h"
#include "modules/bitstream/nalu/nal_unit_splitter.h"
#include "modules/containers/mpegts/mpegts_packet.h"
#include "mpegts_provider_private.h"
#include "base/modules/data_format/scte35_event/scte35_event.h"

namespace pvd
{
	std::shared_ptr<MpegTsStream> MpegTsStream::Create(
		StreamSourceType source_type, channel_id_t client_id,
		const info::VHostAppName &vhost_app_name, const ov::String &stream_name,
		const std::shared_ptr<ov::Socket> &client_socket, const ov::SocketAddressPair &address_pair,
		uint64_t lifetime_epoch_msec, const std::shared_ptr<PushProvider> &provider)
	{
		auto stream = std::make_shared<MpegTsStream>(source_type, client_id, vhost_app_name, stream_name, client_socket, address_pair, lifetime_epoch_msec, provider);
		if (stream != nullptr)
		{
			stream->Start();
		}
		return stream;
	}

	MpegTsStream::MpegTsStream(
		StreamSourceType source_type, channel_id_t client_id,
		const info::VHostAppName &vhost_app_name, const ov::String &stream_name,
		std::shared_ptr<ov::Socket> client_socket, const ov::SocketAddressPair &address_pair,
		uint64_t lifetime_epoch_msec, const std::shared_ptr<PushProvider> &provider)
		: PushStream(source_type, client_id, provider),

		  _vhost_app_name(vhost_app_name)
	{
		SetName(stream_name);
		_remote = client_socket;
		SetMediaSource(ov::String::FormatString("%s://%s", ov::StringFromSocketType(client_socket->GetType()), address_pair.GetRemoteAddress().ToString(false).CStr()));

		// The pair's local side is the per-datagram destination (pktinfo) for the shared UDP
		// listener; fall back to the bind address when it is unavailable.
		// The shared_ptr keeps the fallback alive across the statements below.
		auto bind_address		  = client_socket->GetLocalAddress();
		const auto *local_address = address_pair.GetLocalAddress().IsValid()
										? &address_pair.GetLocalAddress()
										: bind_address.get();
		SetConnectionInfo(info::ConnectionInfo::From(local_address, &address_pair.GetRemoteAddress(), client_socket->GetType()));
		_lifetime_epoch_msec = lifetime_epoch_msec;
	}

	MpegTsStream::~MpegTsStream()
	{
	}

	bool MpegTsStream::Start()
	{
		// Prefix every depacketizer log line with this stream's name path.
		{
			ov::LockGuard<ov::SharedMutex> lock(_depacketizer_lock);
			_depacketizer.SetNamePath(GetNamePath().CStr());
		}

		// UDP datagram reordering is opt-in per application and only applies to datagram-framed(UDP) input;
		// SRT/TCP are byte streams that the transport already delivers in order.
		//
		// Thread-safety note: the depacketizer (including the reorder buffer) has no internal lock;
		// its state is protected by _depacketizer_lock, held here and in OnDataReceived
		// (the member is `OV_GUARDED_BY(_depacketizer_lock)`).
		// Enabling here also runs before the channel is registered, so it happens-before any data reaches the depacketizer.
		if ((_remote != nullptr) && (_remote->GetType() == ov::SocketType::Udp))
		{
			const auto &app_info = ocst::Orchestrator::GetInstance()->GetApplicationInfo(_vhost_app_name);

			if (app_info.IsValid() == false)
			{
				logtd("[%s/%s] Could not resolve application config at stream start; MPEG-TS packet reordering left disabled",
					  _vhost_app_name.CStr(), GetName().CStr());
			}
			else if (app_info.GetConfig().GetProviders().GetMpegtsProvider().GetPacketReordering())
			{
				ov::LockGuard<ov::SharedMutex> lock(_depacketizer_lock);
				_depacketizer.EnablePacketReordering();

				logti("[%s/%s] MPEG-TS UDP packet reordering is enabled", _vhost_app_name.CStr(), GetName().CStr());
			}
		}

		SetState(Stream::State::PLAYING);
		return PushStream::Start();
	}

	bool MpegTsStream::Stop()
	{
		if (GetState() == Stream::State::STOPPED)
		{
			return true;
		}

		// Drain any datagrams the reorder buffer is still holding behind a gap and forward the
		// resulting frames before teardown, so already-received data is delivered rather than
		// dropped. This runs while the stream is still playing (before `PushStream::Stop()`)
		// and is a no-op unless reordering is enabled and something is buffered.
		// `Stop()` is never entered with `_depacketizer_lock` held, so acquiring it here is safe.
		{
			ov::ScopedLock lock(_depacketizer_lock);
			_depacketizer.FlushReorderBuffer();
			ForwardDepacketizedFrames();
		}

		if (_remote->GetState() == ov::SocketState::Connected)
		{
			_remote->Close();
		}

		return PushStream::Stop();
	}

	const std::shared_ptr<ov::Socket> &MpegTsStream::GetClientSock()
	{
		return _remote;
	}

	bool MpegTsStream::OnDataReceived(const std::shared_ptr<const ov::Data> &data)
	{
		if (GetState() == Stream::State::ERROR || GetState() == Stream::State::STOPPED)
		{
			return false;
		}

		if (_lifetime_epoch_msec != 0 &&
			_remote->GetType() == ov::SocketType::Srt &&
			_lifetime_epoch_msec < ov::Clock::NowMSec())
		{
			// Expired
			logti("Stream has expired by signed policy (%s/%s)", _vhost_app_name.CStr(), GetName().CStr());
			Stop();
			return false;
		}

		bool publish_failed = false;
		{
			ov::ScopedLock lock(_depacketizer_lock);
			_depacketizer.AddPacket(data);

			// Publish once the track information becomes available.
			if (IsPublished() == false && _depacketizer.IsTrackInfoAvailable())
			{
				publish_failed = (Publish() == false);
			}

			// Forward whatever is ready. A fatal per-frame error bails out without stopping;
			// a publish failure stops the stream below, outside the lock.
			if ((publish_failed == false) && (ForwardDepacketizedFrames() == false))
			{
				return false;
			}
		}

		if (publish_failed == true)
		{
			// PublishChannel failed. Stop outside the depacketizer lock so the reorder-drain in
			// `Stop()` can acquire it without re-entering (the lock is not held here).
			Stop();
			return false;
		}

		return true;
	}

	bool MpegTsStream::ForwardDepacketizedFrames()
	{
		if (IsPublished() == true)
		{
			while (_depacketizer.IsESAvailable())
			{
				auto es = _depacketizer.PopES();
				auto track = GetTrack(es->PID());

				if (track == nullptr)
				{
					logte("%s/%s(%d) received stream data, but track information could not be found.", GetApplicationName(), GetName().CStr(), GetId());
					return false;
				}

				int64_t origin_pts = es->Pts();
				int64_t origin_dts = es->Dts();
				auto pts = origin_pts;
				auto dts = origin_dts;

				AdjustTimestampByBase(track->GetId(), pts, dts, 0x1FFFFFFFFLL);

				if (es->IsVideoStream())
				{
					auto bitstream = cmn::BitstreamFormat::Unknown;
					auto packet_type = cmn::PacketType::NALU;

					switch (track->GetCodecId())
					{
						case cmn::MediaCodecId::H264:
							bitstream = cmn::BitstreamFormat::H264_ANNEXB;
							break;
						case cmn::MediaCodecId::H265: {
							bitstream = cmn::BitstreamFormat::H265_ANNEXB;
							break;
						}
						default:
							bitstream = cmn::BitstreamFormat::Unknown;
							break;
					}

					auto data = std::make_shared<ov::Data>(es->Payload(), es->PayloadLength());
					auto media_packet = std::make_shared<MediaPacket>(cmn::MediaType::Video,
																	  es->PID(),
																	  data,
																	  pts,
																	  dts,
																	  -1LL,
																	  MediaPacketFlag::Unknown,
																	  bitstream,
																	  packet_type);
					SendFrame(media_packet);
				}
				else if (es->IsAudioStream())
				{
					auto payload		 = es->Payload();
					auto payload_length	 = es->PayloadLength();
					const auto bitstream = track->GetOriginBitstream();

					// A PES of AAC carries one or more ADTS frames, and an fMP4 sample must hold one AAC frame.
					if (bitstream == cmn::BitstreamFormat::AAC_ADTS)
					{
						// Each frame is a copy-on-write slice of the PES payload.
						auto pes_data = std::make_shared<ov::Data>(payload, payload_length);
						const auto split = AacAdtsSplitter::Split(payload, payload_length, track->GetTimeBase().GetDen(), track->GetAudioSamplesPerFrame());

						if (split.stop_reason.IsEmpty() == false)
						{
							logtd("[%s] AAC ADTS splitting of the PES (PID: %d): %s",
								  GetNamePath().CStr(), es->PID(), split.stop_reason.CStr());
						}

						for (const auto &frame : split.frames)
						{
							auto media_packet = std::make_shared<MediaPacket>(
								cmn::MediaType::Audio,
								es->PID(),
								pes_data->Subdata(frame.offset, frame.length),
								pts + frame.timestamp_offset,
								dts + frame.timestamp_offset,
								-1LL,
								MediaPacketFlag::Unknown,
								bitstream,
								cmn::PacketType::RAW);

							SendFrame(media_packet);
						}

						if (split.frames.empty())
						{
							auto media_packet = std::make_shared<MediaPacket>(
								cmn::MediaType::Audio,
								es->PID(),
								pes_data,
								pts,
								dts,
								-1LL,
								MediaPacketFlag::Unknown,
								bitstream,
								cmn::PacketType::RAW);

							SendFrame(media_packet);
						}
					}
					else
					{
						auto data		  = std::make_shared<ov::Data>(payload, payload_length);
						auto media_packet = std::make_shared<MediaPacket>(cmn::MediaType::Audio,
																		  es->PID(),
																		  data,
																		  pts,
																		  dts,
																		  -1LL,
																		  MediaPacketFlag::Unknown,
																		  bitstream,
																		  cmn::PacketType::RAW);
						SendFrame(media_packet);
					}
				}

				logtt("Frame - PID(%d) AdjustPTS(%" PRId64 ") AdjustDTS(%" PRId64 ") PTS(%" PRId64 ") DTS(%" PRId64 ") Size(%d)", es->PID(), pts, dts, origin_pts, origin_dts, es->PayloadLength());
			}
		}

		return true;
	}

	bool MpegTsStream::Publish()
	{
		std::map<uint16_t, std::shared_ptr<MediaTrack>> track_list;

		if (_depacketizer.GetTrackList(&track_list) == false)
		{
			logte("Cannot get track list from mpeg-ts depacketizer.");
			return false;
		}

		for (const auto &x : track_list)
		{
			auto track = x.second;
			AddTrack(track);
		}

		// Publish
		if (PublishChannel(_vhost_app_name) == false)
		{
			// The caller stops the stream (outside the depacketizer lock).
			return false;
		}

		return true;
	}
}  // namespace pvd
