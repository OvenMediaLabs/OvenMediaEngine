//==============================================================================
//
//  WebRTC Provider
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#include "webrtc_stream.h"

#include "base/ovlibrary/random.h"
#include "modules/rtp_rtcp/rtcp_info/sender_report.h"
#include "webrtc_application.h"
#include "webrtc_private.h"

namespace pvd
{
	std::shared_ptr<WebRTCStream> WebRTCStream::Create(StreamSourceType source_type, ov::String stream_name,
													   const std::shared_ptr<PushProvider> &provider,
													   const std::shared_ptr<const SessionDescription> &local_sdp,
													   const std::shared_ptr<const SessionDescription> &remote_sdp,
													   const std::shared_ptr<Certificate> &certificate,
													   const std::shared_ptr<IcePort> &ice_port,
													   session_id_t ice_session_id)
	{
		auto stream = std::make_shared<WebRTCStream>(source_type, stream_name, provider, local_sdp, remote_sdp, certificate, ice_port, ice_session_id);
		if (stream != nullptr)
		{
			if (stream->Start() == false)
			{
				return nullptr;
			}
		}
		return stream;
	}

	WebRTCStream::WebRTCStream(StreamSourceType source_type, ov::String stream_name,
							   const std::shared_ptr<PushProvider> &provider,
							   const std::shared_ptr<const SessionDescription> &local_sdp,
							   const std::shared_ptr<const SessionDescription> &remote_sdp,
							   const std::shared_ptr<Certificate> &certificate,
							   const std::shared_ptr<IcePort> &ice_port,
							   session_id_t ice_session_id)
		: PushStream(source_type, stream_name, provider), Node(NodeType::Edge)
	{
		_local_sdp = local_sdp;
		_remote_sdp = remote_sdp;

		if (_local_sdp->GetType() == SessionDescription::SdpType::Offer)
		{
			_offer_sdp = _local_sdp;
			_answer_sdp = _remote_sdp;
		}
		else
		{
			_offer_sdp = _remote_sdp;
			_answer_sdp = _local_sdp;
		}

		_ice_port = ice_port;
		_certificate = certificate;
		_session_key = ov::Random::GenerateString(8);
		_ice_session_id = ice_session_id;

		_h264_bitstream_parser.SetConfig(H264BitstreamParser::Config{._parse_slice_type = true});
	}

	WebRTCStream::~WebRTCStream()
	{
		logtt("WebRTCStream(%s/%d) is destroyed", GetName().CStr(), GetId());
	}

	bool WebRTCStream::Start()
	{
		std::lock_guard<std::shared_mutex> lock(_start_stop_lock);

		logtt("[WebRTC Provider] Local SDP");
		logtt("%s\n", _local_sdp->ToString().CStr());
		logtt("[WebRTC Provider] Peer SDP");
		logtt("%s", _remote_sdp->ToString().CStr());

		auto local_media_desc_list = _local_sdp->GetMediaList();
		auto remote_media_desc_list = _remote_sdp->GetMediaList();
		auto answer_media_desc_list = _answer_sdp->GetMediaList();

		if (local_media_desc_list.size() != remote_media_desc_list.size())
		{
			logte("m= line of peer does not correspond with local");
			return false;
		}

		// Create Nodes
		_rtp_rtcp = std::make_shared<RtpRtcp>(RtpRtcpInterface::GetSharedPtr());
		_srtp_transport = std::make_shared<SrtpTransport>();
		_dtls_transport = std::make_shared<DtlsTransport>();

		auto application = std::static_pointer_cast<WebRTCApplication>(GetApplication());
		_dtls_transport->SetLocalCertificate(_certificate);
		_dtls_transport->StartDTLS();

		// RFC3264
		// For each "m=" line in the local, there MUST be a corresponding "m=" line in the peer.
		for (size_t i = 0; i < remote_media_desc_list.size(); i++)
		{
			auto remote_media_desc = remote_media_desc_list[i];
			auto local_media_desc = local_media_desc_list[i];
			auto offer_media_desc = local_media_desc_list[i];
			auto answer_media_desc = answer_media_desc_list[i];

			if(remote_media_desc->GetDirection() != MediaDescription::Direction::SendOnly &&
				remote_media_desc->GetDirection() != MediaDescription::Direction::SendRecv)
			{
				logtt("Media (%s) is inactive", remote_media_desc->GetMediaTypeStr().CStr());
				continue;
			}

			// Create Channel

			// Simulcast Layer
			if (local_media_desc->GetRecvLayerList().size() == 0)
			{
				auto first_payload = answer_media_desc->GetFirstPayload();
				if (first_payload == nullptr)
				{
					logte("Could not find payload in the media description");
					continue;
				}
				
				if (CreateChannel(remote_media_desc, local_media_desc, nullptr, first_payload) == false)
				{
					logte("Could not create channel : pt(%d)", first_payload->GetId());
					continue;
				}
			}
			else
			{
				for (auto &layer : local_media_desc->GetRecvLayerList())
				{
					// OME doesn't support alternative rid and alternative pt
					// It already has been discarded in the SDP
					auto first_rid = layer->GetFirstRid();
					if (first_rid == nullptr)
					{
						logte("Could not find RID in the simulcast layer");
						continue;
					}

					auto first_pt = first_rid->GetFirstPt();
					if (first_pt.has_value() == false)	
					{
						// If there is no payload type, use the first payload type of the media description.
						auto first_payload = local_media_desc->GetFirstPayload();
						if (first_payload == nullptr)
						{
							logte("Could not find payload in the RID of the simulcast layer : rid(%s)", first_rid->GetId().CStr());
							continue;
						}

						first_pt = first_payload->GetId();
					}

					auto payload_attr = local_media_desc->GetPayload(first_pt.value());
					if (payload_attr == nullptr)
					{
						logte("Could not find payload in the RID of the simulcast layer : rid(%s), pt(%d)", first_rid->GetId().CStr(), first_pt.value());
						continue;
					}

					if (CreateChannel(offer_media_desc, answer_media_desc, first_rid, payload_attr) == false)
					{
						logte("Could not create channel : rid(%s), pt(%d)", first_rid->GetId().CStr(), first_pt.value());
						continue;
					}
				}
			}
		}

		// Connect nodes
		_rtp_rtcp->RegisterPrevNode(nullptr);
		_rtp_rtcp->RegisterNextNode(_srtp_transport);
		_rtp_rtcp->Start();
		_srtp_transport->RegisterPrevNode(_rtp_rtcp);
		_srtp_transport->RegisterNextNode(_dtls_transport);
		_srtp_transport->Start();
		_dtls_transport->RegisterPrevNode(_srtp_transport);
		_dtls_transport->RegisterNextNode(ov::Node::GetSharedPtr());
		_dtls_transport->Start();

		RegisterPrevNode(_dtls_transport);
		RegisterNextNode(nullptr);
		ov::Node::Start();

		_fir_timer.Start();

		// _sent_sequence_header = false;

		return pvd::Stream::Start();
	}

	bool WebRTCStream::CreateChannel(const std::shared_ptr<const MediaDescription> &remote_media_desc, 
						const std::shared_ptr<const MediaDescription> &local_media_desc,
						const std::shared_ptr<const RidAttr> &rid_attr, /* Optional / can be nullptr */
						const std::shared_ptr<const PayloadAttr> &payload_attr)
	{
		auto offer_media_desc = _local_sdp->GetType() == SessionDescription::SdpType::Offer ? local_media_desc : remote_media_desc;
		auto answer_media_desc = _local_sdp->GetType() == SessionDescription::SdpType::Offer ? remote_media_desc : local_media_desc;

		auto track = CreateTrack(payload_attr);
		if (track == nullptr)
		{
			logte("Could not create track : pt(%d)", payload_attr->GetId());
			return false;
		}

		if (AddTrack(track) == false)
		{
			logte("Could not add track : pt(%d)", payload_attr->GetId());
			return false;
		}

		// Add Depacketizer
		if (AddDepacketizer(track->GetId()) == false)
		{
			logte("Could not add depacketizer : pt(%d)", payload_attr->GetId());
			return false;
		}

		if (_rtp_rtcp->IsTransportCcFeedbackEnabled() == false && payload_attr->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::TransportCc) == true)
		{
			// a=extmap:id http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
			uint8_t transport_cc_extension_id = 0;
			ov::String transport_cc_extension_uri;
			if (answer_media_desc->FindExtmapItem("transport-wide-cc-extensions", transport_cc_extension_id, transport_cc_extension_uri) == true)
			{
				_rtp_rtcp->EnableTransportCcFeedback(transport_cc_extension_id);
			}
		}

		// uri:ietf:rtc:rtp-hdrext:video:CompositionTime
		ov::String cts_extmap_uri;
		if (answer_media_desc->FindExtmapItem("CompositionTime", _cts_extmap_id, cts_extmap_uri))
		{
			_cts_extmap_enabled = true;
		}

		// mid extension
		uint8_t mid_extension_id = 0;
		ov::String mid_extension_uri;
		bool has_mid_extension = answer_media_desc->FindExtmapItem("urn:ietf:params:rtp-hdrext:sdes:mid", mid_extension_id, mid_extension_uri);

		// rid extension
		uint8_t rid_extension_id = 0;
		ov::String rid_extension_uri;
		bool has_rid_extension = false;
		if (rid_attr != nullptr)
		{
			has_rid_extension = answer_media_desc->FindExtmapItem("urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id", rid_extension_id, rid_extension_uri);
		}

		// Add Rtp Receiver
		RtpRtcp::RtpTrackIdentifier rtp_track_id(track->GetId());

		rtp_track_id.ssrc = remote_media_desc->GetSsrc();
		rtp_track_id.cname = answer_media_desc->GetCname();
		rtp_track_id.mid = has_mid_extension ? answer_media_desc->GetMid() : std::nullopt;
		rtp_track_id.mid_extension_id = mid_extension_id;
		rtp_track_id.rid = has_rid_extension ? std::optional<ov::String>(rid_attr->GetId()) : std::nullopt;
		rtp_track_id.rid_extension_id = rid_extension_id;

		if (_rtp_rtcp->AddRtpReceiver(track, rtp_track_id) == false)
		{
			logte("Could not add rtp receiver : track_id(%u)", track->GetId());
			return false;
		}

		// Clock
		RegisterRtpClock(track->GetId(), track->GetTimeBase().GetExpr());

		return true;
	}

	std::shared_ptr<MediaTrack> WebRTCStream::CreateTrack(const std::shared_ptr<const PayloadAttr> &payload_attr)
	{
		auto track = std::make_shared<MediaTrack>();

		track->SetId(IssueUniqueTrackId());
		track->SetTimeBase(1, payload_attr->GetCodecRate());

		auto codec = payload_attr->GetCodec();
		if (codec == PayloadAttr::SupportCodec::OPUS)
		{
			track->SetMediaType(cmn::MediaType::Audio);
			track->SetCodecId(cmn::MediaCodecId::Opus);
			track->SetOriginBitstream(cmn::BitstreamFormat::OPUS_RTP_RFC_7587);
		}
		else if (codec == PayloadAttr::SupportCodec::MPEG4_GENERIC)
		{
			track->SetMediaType(cmn::MediaType::Audio);
			track->SetCodecId(cmn::MediaCodecId::Aac);
			track->SetOriginBitstream(cmn::BitstreamFormat::AAC_MPEG4_GENERIC);
		}
		else if (codec == PayloadAttr::SupportCodec::H264)
		{
			track->SetMediaType(cmn::MediaType::Video);
			track->SetCodecId(cmn::MediaCodecId::H264);
			track->SetOriginBitstream(cmn::BitstreamFormat::H264_RTP_RFC_6184);
			track->SetVideoTimestampScale(1.0);
		}
		else if (codec == PayloadAttr::SupportCodec::H265)
		{
			track->SetMediaType(cmn::MediaType::Video);
			track->SetCodecId(cmn::MediaCodecId::H265);
			track->SetOriginBitstream(cmn::BitstreamFormat::H265_RTP_RFC_7798);
			track->SetVideoTimestampScale(1.0);
		}
		else if (codec == PayloadAttr::SupportCodec::VP8)
		{
			track->SetMediaType(cmn::MediaType::Video);
			track->SetCodecId(cmn::MediaCodecId::Vp8);
			track->SetOriginBitstream(cmn::BitstreamFormat::VP8_RTP_RFC_7741);
			track->SetVideoTimestampScale(1.0);
		}
		else
		{
			logte("%s - Unsupported codec : codec(%d)", GetName().CStr(), static_cast<uint8_t>(codec));
			return nullptr;
		}

		if (track->GetMediaType() == cmn::MediaType::Audio)
		{	
			// channel
			auto channels = std::atoi(payload_attr->GetCodecParams());
			if (channels == 1)
			{
				track->GetChannel().SetLayout(cmn::AudioChannel::Layout::LayoutMono);
			}
			else
			{
				track->GetChannel().SetLayout(cmn::AudioChannel::Layout::LayoutStereo);
			}
		}

		return track;
	}

	ov::String WebRTCStream::GetSessionKey() const
	{
		return _session_key;
	}

	void WebRTCStream::SetOvenCapabilities(const ov::String &capabilities)
	{
		_oven_capabilities = capabilities;

		// 26.01.08
		// Oven-Capabilities: max_width=1920, max_height=1080

		// Parse and apply capabilities
		logtt("%s - Set Oven-Capabilities: %s", GetName().CStr(), capabilities.CStr());

		auto params = ov::String::Split(capabilities.CStr(), ",");
		for (const auto &param : params)
		{
			auto key_value = ov::String::Split(param.CStr(), "=");
			if (key_value.size() != 2)
			{
				continue;
			}

			auto key = key_value[0].Trim().LowerCaseString();
			auto value = key_value[1].Trim();
			if (key == "max_width")
			{
				auto first_video_track = GetFirstTrackByType(cmn::MediaType::Video);
				if (first_video_track != nullptr)
				{
					first_video_track->SetWidth(static_cast<uint32_t>(std::atoi(value.CStr())));
					logtt("%s - Set max width: %u", GetName().CStr(), first_video_track->GetMaxWidth());
				}
			}
			else if (key == "max_height")
			{
				auto first_video_track = GetFirstTrackByType(cmn::MediaType::Video);
				if (first_video_track != nullptr)
				{
					first_video_track->SetHeight(static_cast<uint32_t>(std::atoi(value.CStr())));
					logtt("%s - Set max height: %u", GetName().CStr(), first_video_track->GetMaxHeight());
				}
			}
		}
	}

	bool WebRTCStream::AddDepacketizer(uint32_t track_id)
	{
		auto track = GetTrack(track_id);
		RtpDepacketizingManager::SupportedDepacketizerType depacketizer_codec_id;

		switch (track->GetCodecId())
		{
			case cmn::MediaCodecId::H264:
				depacketizer_codec_id = RtpDepacketizingManager::SupportedDepacketizerType::H264;
				break;

			case cmn::MediaCodecId::H265:
				depacketizer_codec_id = RtpDepacketizingManager::SupportedDepacketizerType::H265;
				break;

			case cmn::MediaCodecId::Opus:
				depacketizer_codec_id = RtpDepacketizingManager::SupportedDepacketizerType::OPUS;
				break;

			case cmn::MediaCodecId::Vp8:
				depacketizer_codec_id = RtpDepacketizingManager::SupportedDepacketizerType::VP8;
				break;

			case cmn::MediaCodecId::Aac:
				depacketizer_codec_id = RtpDepacketizingManager::SupportedDepacketizerType::MPEG4_GENERIC_AUDIO;
				break;

			default:
				logte("%s - Unsupported codec : codec(%d)", GetName().CStr(), static_cast<uint8_t>(track->GetCodecId()));
				return false;
		}

		auto depacketizer = RtpDepacketizingManager::Create(depacketizer_codec_id);
		if (depacketizer == nullptr)
		{
			logte("%s - Could not create depacketizer : codec_id(%d)", GetName().CStr(), static_cast<uint8_t>(depacketizer_codec_id));
			return false;
		}

		_depacketizers[track_id] = depacketizer;

		return true;
	}

	std::shared_ptr<RtpDepacketizingManager> WebRTCStream::GetDepacketizer(uint32_t track_id)
	{
		auto it = _depacketizers.find(track_id);
		if (it == _depacketizers.end())
		{
			return nullptr;
		}

		return it->second;
	}

	bool WebRTCStream::Stop()
	{
		std::lock_guard<std::shared_mutex> lock(_start_stop_lock);

		if (GetState() == Stream::State::STOPPED || GetState() == Stream::State::TERMINATED)
		{
			return true;
		}

		if (_rtp_rtcp != nullptr)
		{
			_rtp_rtcp->Stop();
		}

		if (_dtls_transport != nullptr)
		{
			_dtls_transport->Stop();
		}

		if (_srtp_transport != nullptr)
		{
			_srtp_transport->Stop();
		}

		_ice_port->DisconnectSession(_ice_session_id);

		ov::Node::Stop();

		return pvd::Stream::Stop();
	}

	std::shared_ptr<const SessionDescription> WebRTCStream::GetLocalSDP()
	{
		return _local_sdp;
	}

	std::shared_ptr<const SessionDescription> WebRTCStream::GetPeerSDP()
	{
		return _remote_sdp;
	}

	bool WebRTCStream::OnDataReceived(const std::shared_ptr<const ov::Data> &data)
	{
		logtp("OnDataReceived (%d)", data->GetLength());
		// To DTLS -> SRTP -> RTP|RTCP -> WebRTCStream::OnRtxpReceived

		//It must not be called during start and stop.
		std::shared_lock<std::shared_mutex> lock(_start_stop_lock);

		return SendDataToPrevNode(data);
	}

	// From RtpRtcp node
	void WebRTCStream::OnRtpFrameReceived(const std::vector<std::shared_ptr<RtpPacket>> &rtp_packets)
	{
		auto first_rtp_packet = rtp_packets.front();
		auto ssrc = first_rtp_packet->Ssrc();
		auto track_id_opt = _rtp_rtcp->GetTrackId(ssrc);
		if (track_id_opt.has_value() == false)
		{
			logte("%s - Could not find track id : ssrc(%u)", GetName().CStr(), ssrc);
			return;
		}
		auto track_id = track_id_opt.value();

		logtp("%s", first_rtp_packet->Dump().CStr());

		auto track = GetTrack(track_id);
		if (track == nullptr)
		{
			logte("%s - Could not find track : ssrc(%u)", GetName().CStr(), ssrc);
			return;
		}

		auto depacketizer = GetDepacketizer(track_id);
		if (depacketizer == nullptr)
		{
			logte("%s - Could not find depacketizer : ssrc(%u", GetName().CStr(), ssrc);
			return;
		}

		std::vector<std::shared_ptr<ov::Data>> payload_list;
		for (const auto &packet : rtp_packets)
		{
			logtp("%s", packet->Dump().CStr());
			auto payload = std::make_shared<ov::Data>(packet->Payload(), packet->PayloadSize());
			payload_list.push_back(payload);
		}

		auto bitstream = depacketizer->ParseAndAssembleFrame(payload_list);
		if (bitstream == nullptr)
		{
			logte("%s - Could not depacketize packet : ssrc(%u)", GetName().CStr(), ssrc);
			return;
		}

		cmn::BitstreamFormat bitstream_format;
		cmn::PacketType packet_type;

		bool cts_enabled = false;
		switch (track->GetCodecId())
		{
			case cmn::MediaCodecId::H265:
				// Our H265 depacketizer always converts packet to Annex B
				bitstream_format = cmn::BitstreamFormat::H265_ANNEXB;
				packet_type = cmn::PacketType::NALU;
				cts_enabled = _cts_extmap_enabled == true;
				if( _h26x_extradata_nalu.find(track->GetId()) == _h26x_extradata_nalu.end())
				{
					_h26x_extradata_nalu[track->GetId()] = depacketizer->GetDecodingParameterSetsToAnnexB();
				}
				break;

			case cmn::MediaCodecId::H264:
				// Our H264 depacketizer always converts packet to Annex B
				bitstream_format = cmn::BitstreamFormat::H264_ANNEXB;
				packet_type = cmn::PacketType::NALU;
				cts_enabled = _cts_extmap_enabled == true;
				if (_h26x_extradata_nalu.find(track->GetId()) == _h26x_extradata_nalu.end())
				{
					_h26x_extradata_nalu[track->GetId()] = depacketizer->GetDecodingParameterSetsToAnnexB();
				}
				break;

			case cmn::MediaCodecId::Opus:
				bitstream_format = cmn::BitstreamFormat::OPUS;
				packet_type = cmn::PacketType::RAW;
				break;

			case cmn::MediaCodecId::Vp8:
				bitstream_format = cmn::BitstreamFormat::VP8;
				packet_type = cmn::PacketType::RAW;
				break;

			// It can't be reached here because it has already failed in GetDepacketizer.
			default:
				return;
		}

		int64_t adjusted_timestamp;
		if (AdjustRtpTimestamp(track_id, first_rtp_packet->Timestamp(), std::numeric_limits<uint32_t>::max(), adjusted_timestamp) == false)
		{
			// Prevents the stream from being deleted because there is no input data
			MonitorInstance->IncreaseBytesIn(*Stream::GetSharedPtr(), bitstream->GetLength());
			return;
		}
		
		int64_t dts = adjusted_timestamp;
		if (cts_enabled == true)
		{
			auto cts_extension_opt = first_rtp_packet->GetExtension<int24_t>(_cts_extmap_id);
			if (cts_extension_opt.has_value() == true)
			{
				int32_t cts = cts_extension_opt.value();
				dts = adjusted_timestamp - (cts * 90);
				logtt("PTS(%lld) CTS(%d) DTS(%lld)", adjusted_timestamp, cts, dts);
			}
			else
			{
				logte("CTS is enabled but not found in the packet");
			}
		}
		
		logtt("Payload Type(%d) Timestamp(%u) PTS(%u) Time scale(%f) Adjust Timestamp(%f)",
			  first_rtp_packet->PayloadType(), first_rtp_packet->Timestamp(), adjusted_timestamp, track->GetTimeBase().GetExpr(), static_cast<double>(adjusted_timestamp) * track->GetTimeBase().GetExpr());

		auto frame = std::make_shared<MediaPacket>(GetMsid(),
												   track->GetMediaType(),
												   track->GetId(),
												   bitstream,
												   adjusted_timestamp,
												   dts,
												   -1LL,
												   MediaPacketFlag::Unknown,
												   bitstream_format,
												   packet_type);

		// Reorder frames in DTS order
		if (cts_enabled == true)
		{
			// PTS order to DTS order
			// Q and Flush (if slice type is I or P)
			_dts_ordered_frame_buffer.emplace(dts, frame);

			switch (bitstream_format)
			{
				case cmn::BitstreamFormat::H264_ANNEXB:
				{
					if (_h264_bitstream_parser.Parse(bitstream) == true)
					{
						auto last_slice_type = _h264_bitstream_parser.GetLastSliceType();

						logtt("PTS(%lld) DTS(%lld) Slice Type(%d)", adjusted_timestamp, dts, last_slice_type.has_value()?static_cast<int>(last_slice_type.value()):-1);

						if (last_slice_type.has_value() == true && last_slice_type.value() != H264SliceType::B)
						{
							// Flush All
							for (auto &frame : _dts_ordered_frame_buffer)
							{
								OnFrame(track, frame.second);
							}
							_dts_ordered_frame_buffer.clear();
						}
					}

					return;
				}
				case cmn::BitstreamFormat::H265_ANNEXB:
				{
					// logtw("H265 is not supported yet");
					return;
				}
				default:
					break;
			}
		}

		OnFrame(track, frame);
	}

	void WebRTCStream::OnFrame(const std::shared_ptr<MediaTrack> &track, const std::shared_ptr<MediaPacket> &media_packet)
	{
		logtp("Send Frame : track_id(%d) codec_id(%d) bitstream_format(%d) packet_type(%d) data_length(%d) pts(%u)", track->GetId(), track->GetCodecId(), media_packet->GetBitstreamFormat(), media_packet->GetPacketType(), media_packet->GetDataLength(), media_packet->GetPts());

		// This may not work since almost WebRTC browser sends SPS/PPS/VPS in-band
		if ((track->GetCodecId() == cmn::MediaCodecId::H264 || track->GetCodecId() == cmn::MediaCodecId::H265) &&
			_sent_sequence_header.find(track->GetId()) == _sent_sequence_header.end())
		{
			if (_h26x_extradata_nalu.find(track->GetId()) != _h26x_extradata_nalu.end() && _h26x_extradata_nalu[track->GetId()] != nullptr)
			{
				auto bitstream_format = (track->GetCodecId() == cmn::MediaCodecId::H264) ? cmn::BitstreamFormat::H264_ANNEXB : cmn::BitstreamFormat::H265_ANNEXB;
				auto sps_pps_packet = std::make_shared<MediaPacket>(GetMsid(),
																	track->GetMediaType(),
																	track->GetId(),
																	_h26x_extradata_nalu[track->GetId()],
																	media_packet->GetPts(),
																	media_packet->GetDts(),
																	-1LL,
																	MediaPacketFlag::Unknown,
																	bitstream_format,
																	cmn::PacketType::NALU);
				SendFrame(sps_pps_packet);
			}
			
			_sent_sequence_header[track->GetId()] = true;
		}

		SendFrame(media_packet);

		// Send FIR to reduce keyframe interval
		if (_fir_timer.IsElapsed(3000) && track->GetMediaType() == cmn::MediaType::Video)
		{
			_fir_timer.Update();
			//_rtp_rtcp->SendPLI(first_rtp_packet->Ssrc());
			_rtp_rtcp->SendFIR(track->GetId());
		}

		// Send Receiver Report
	}

	// From RtpRtcp node
	void WebRTCStream::OnRtcpReceived(const std::shared_ptr<RtcpInfo> &rtcp_info)
	{
		// Receive Sender Report
		if (rtcp_info->GetPacketType() == RtcpPacketType::SR)
		{
			auto sr = std::dynamic_pointer_cast<SenderReport>(rtcp_info);

			auto track_id = _rtp_rtcp->GetTrackId(sr->GetSenderSsrc());
			if (track_id.has_value() == false)
			{
				// This can happen if RTCP arrives before RTP
				logtw("%s - Could not find track id for RTCP SR : ssrc(%u)", GetName().CStr(), sr->GetSenderSsrc());
				return;
			}

			UpdateSenderReportTimestamp(track_id.value(), sr->GetMsw(), sr->GetLsw(), sr->GetTimestamp());
		}
	}

	// ov::Node Interface
	// RtpRtcp -> SRTP -> DTLS -> Edge(this) -> IcePort
	bool WebRTCStream::OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data)
	{
		if (ov::Node::GetNodeState() != ov::Node::NodeState::Started)
		{
			logtt("Node has not started, so the received data has been canceled.");
			return false;
		}

		return _ice_port->Send(_ice_session_id, data);
	}

	// WebRTCStream Node has not a lower node so it will not be called
	bool WebRTCStream::OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data)
	{
		return true;
	}
}  // namespace pvd