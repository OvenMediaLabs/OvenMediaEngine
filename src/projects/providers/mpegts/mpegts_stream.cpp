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
#include "modules/bitstream/aac/aac_adts.h"
#include "modules/bitstream/h265/h265_parser.h"
#include "modules/bitstream/nalu/nal_unit_splitter.h"
#include "modules/containers/mpegts/mpegts_packet.h"
#include "mpegts_provider_private.h"
#include "base/modules/data_format/scte35_event/scte35_event.h"

namespace pvd
{
	std::shared_ptr<MpegTsStream> MpegTsStream::Create(StreamSourceType source_type, uint32_t client_id, const info::VHostAppName &vhost_app_name, const ov::String &stream_name, const std::shared_ptr<ov::Socket> &client_socket, const ov::SocketAddress &remote_address, uint64_t lifetime_epoch_msec, const std::shared_ptr<PushProvider> &provider)
	{
		auto stream = std::make_shared<MpegTsStream>(source_type, client_id, vhost_app_name, stream_name, client_socket, remote_address, lifetime_epoch_msec, provider);
		if (stream != nullptr)
		{
			stream->Start();
		}
		return stream;
	}

	MpegTsStream::MpegTsStream(StreamSourceType source_type, uint32_t client_id, const info::VHostAppName &vhost_app_name, const ov::String &stream_name, std::shared_ptr<ov::Socket> client_socket, const ov::SocketAddress &remote_address, uint64_t lifetime_epoch_msec, const std::shared_ptr<PushProvider> &provider)
		: PushStream(source_type, client_id, provider),

		  _vhost_app_name(vhost_app_name)
	{
		SetName(stream_name);
		_remote = client_socket;
		SetMediaSource(ov::String::FormatString("%s://%s", ov::StringFromSocketType(client_socket->GetType()), remote_address.ToString(false).CStr()));
		_lifetime_epoch_msec = lifetime_epoch_msec;
	}

	MpegTsStream::~MpegTsStream()
	{
	}

	bool MpegTsStream::Start()
	{
		SetState(Stream::State::PLAYING);
		return PushStream::Start();
	}

	bool MpegTsStream::Stop()
	{
		if (GetState() == Stream::State::STOPPED)
		{
			return true;
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

		ov::LockGuard<ov::SharedMutex> lock(_depacketizer_lock);
		_depacketizer.AddPacket(data);

		// Publish
		if (IsPublished() == false && _depacketizer.IsTrackInfoAvailable())
		{
			if (Publish() == false)
			{
				return false;
			}
		}

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
					auto media_packet = std::make_shared<MediaPacket>(GetMsid(),
																	  cmn::MediaType::Video,
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

					// A single MPEG-TS PES payload for AAC carries one or more ADTS frames.
					// fMP4/LL-HLS packaging requires one access unit (1024 samples) per sample,
					// so split the PES into individual ADTS frames here.
					// Otherwise the muxer emits a single oversized sample per PES (N frames glued together),
					// which strict fMP4 audio decoders (e.g. Safari) reject.
					if (bitstream == cmn::BitstreamFormat::AAC_ADTS)
					{
						const int64_t timescale			= track->GetTimeBase().GetDen();
						const int64_t samples_per_frame = (track->GetAudioSamplesPerFrame() > 0) ? track->GetAudioSamplesPerFrame() : 1024;

						// Forward each frame as a copy-on-write slice of the PES payload (no per-frame copy).
						auto pes_data					= std::make_shared<ov::Data>(payload, payload_length);

						// frame_duration is resolved once from the first ADTS header; the sample rate is constant per track.
						int64_t frame_duration			= 0;
						size_t offset					= 0;
						auto frame_pts					= pts;
						auto frame_dts					= dts;

						while ((offset + ADTS_MIN_SIZE) <= payload_length)
						{
							AACAdts adts;

							if (AACAdts::Parse(payload + offset, payload_length - offset, adts) == false)
							{
								logtd(
									"[%s] Stopped AAC ADTS splitting: no valid ADTS header at offset %zu/%u (PID: %d). "
									"The PES is likely truncated or corrupted (e.g. UDP packet loss).",
									GetNamePath().CStr(), offset, payload_length, es->PID());
								break;
							}

							const auto frame_length = adts.AacFrameLength();

							if ((frame_length < ADTS_MIN_SIZE) || ((offset + frame_length) > payload_length))
							{
								logtd(
									"[%s] Stopped AAC ADTS splitting: frame length %u at offset %zu exceeds PES payload %u (PID: %d). "
									"The PES is likely truncated or corrupted (e.g. UDP packet loss).",
									GetNamePath().CStr(), static_cast<uint32_t>(frame_length), offset, payload_length, es->PID());
								break;
							}

							// Resolve the per-frame duration once; the sample rate is constant within a track.
							//
							// NOTE: an invalid sampling-frequency index trips OV_ASSERT2() in debug builds, which is
							// intentional (it surfaces malformed input).
							// Release builds return 0. A non-positive duration (invalid sample rate or timebase) means we cannot time the frames,
							// so we stop splitting and forward the whole PES unsplit below instead of emitting identical-timestamp samples.
							if (frame_duration == 0)
							{
								const auto samplerate = adts.Samplerate();

								if (samplerate != 0)
								{
									frame_duration = cmn::Rational::Rescale(samples_per_frame, cmn::Rational(1, static_cast<int32_t>(samplerate)), cmn::Rational(1, static_cast<int32_t>(timescale)));
								}

								if (frame_duration <= 0)
								{
									break;
								}
							}

							auto media_packet = std::make_shared<MediaPacket>(
								GetMsid(),
								cmn::MediaType::Audio,
								es->PID(),
								pes_data->Subdata(offset, frame_length),
								frame_pts,
								frame_dts,
								-1LL,
								MediaPacketFlag::Unknown,
								bitstream,
								cmn::PacketType::RAW);

							SendFrame(media_packet);

							frame_pts += frame_duration;
							frame_dts += frame_duration;
							offset += frame_length;
						}

						if (offset == 0)
						{
							// No frame could be emitted (unparseable first frame, payload shorter than an ADTS header, or indeterminable timing):
							// forward the whole PES unsplit, preserving the previous behavior.
							auto media_packet = std::make_shared<MediaPacket>(
								GetMsid(),
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
						else if ((offset < payload_length) && ((payload_length - offset) < ADTS_MIN_SIZE))
						{
							// The loop ended on a truncated tail: fewer than one ADTS header (`< ADTS_MIN_SIZE`) remained.
							// A mid-PES break on a malformed header/length leaves `>= ADTS_MIN_SIZE` bytes and was already
							// logged at the break, so it is excluded here to avoid double logging.
							logtd("[%s] Dropped %zu trailing byte(s) of the AAC PES after splitting (offset %zu/%u, PID: %d).",
								  GetNamePath().CStr(), static_cast<size_t>(payload_length - offset), offset, payload_length, es->PID());
						}
					}
					else
					{
						auto data		  = std::make_shared<ov::Data>(payload, payload_length);
						auto media_packet = std::make_shared<MediaPacket>(GetMsid(),
																		  cmn::MediaType::Audio,
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
			Stop();
			return false;
		}

		return true;
	}
}  // namespace pvd
