//==============================================================================
//
//  WebRTC Provider
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#include "webrtc_private.h"
#include "webrtc_application.h"

#include <modules/rtp_rtcp/rtp_header_extension/rtp_header_extension.h>

namespace pvd
{
	std::shared_ptr<WebRTCApplication> WebRTCApplication::Create(const std::shared_ptr<pvd::PushProvider> &provider, const info::Application &application_info, const std::shared_ptr<Certificate> &certificate, const std::shared_ptr<IcePort> &ice_port, const std::shared_ptr<RtcSignallingServer> &rtc_signalling)
	{
		auto application = std::make_shared<WebRTCApplication>(provider, application_info, certificate, ice_port, rtc_signalling);
		if(application->Start() == false)
		{
			return nullptr;
		}
		return application;
	}

	WebRTCApplication::WebRTCApplication(const std::shared_ptr<pvd::PushProvider> &provider, const info::Application &application_info, const std::shared_ptr<Certificate> &certificate, const std::shared_ptr<IcePort> &ice_port, const std::shared_ptr<RtcSignallingServer> &rtc_signalling)
		: PushApplication(provider, application_info)
	{
		_ice_port = ice_port;
		_rtc_signalling = rtc_signalling;
		_certificate = certificate;
	}

	bool WebRTCApplication::Start()
	{
		_offer_sdp = CreateOfferSDP();

		if(_certificate == nullptr || _offer_sdp == nullptr)
		{
			logte("Failed to generate certificate and offer SDP. Could not start %s/s", GetApplicationTypeName(), GetVHostAppName().CStr());
			return false;
		}

		return Application::Start();
	}

	bool WebRTCApplication::Stop()
	{
		return Application::Stop();
	}

	std::shared_ptr<const SessionDescription> WebRTCApplication::GetOfferSDP()
	{
		return _offer_sdp;
	}

	std::shared_ptr<SessionDescription> WebRTCApplication::CreateOfferSDP()
	{
		if(_certificate == nullptr)
		{
			return nullptr;
		}

		bool transport_cc_enabled = true;
		bool composition_time_enabled = true;

		auto offer_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer);
		offer_sdp->SetOrigin("OvenMediaEngine", ov::Random::GenerateUInt32(), 2, "IN", 4, "127.0.0.1");
		offer_sdp->SetExtmapAllowMixed(true);
		offer_sdp->SetTiming(0, 0);
		offer_sdp->SetIceOption("trickle");
		offer_sdp->SetIceUfrag(ov::Random::GenerateString(8));
		offer_sdp->SetIcePwd(ov::Random::GenerateString(32));

		// MSID
		auto msid = ov::Random::GenerateString(36);
		offer_sdp->SetMsidSemantic("WMS", msid);
		offer_sdp->SetFingerprint("sha-256", _certificate->GetFingerprint("sha-256"));

		uint8_t payload_type_num = 100;

		auto cname = ov::Random::GenerateString(16);

		///////////////////////////////////////
		// Video Media Description
		///////////////////////////////////////
		auto video_media_desc = std::make_shared<MediaDescription>();
		video_media_desc->SetMediaType(MediaDescription::MediaType::Video);
		video_media_desc->SetConnection(4, "0.0.0.0");
		video_media_desc->SetMid(ov::Random::GenerateString(6));
		video_media_desc->SetMsid(msid, ov::Random::GenerateString(36));
		video_media_desc->SetSetup(MediaDescription::SetupType::ActPass);
		video_media_desc->UseDtls(true);
		video_media_desc->UseRtcpMux(true);
		video_media_desc->SetDirection(MediaDescription::Direction::RecvOnly);
		if (transport_cc_enabled)
		{
			video_media_desc->AddExtmap(RTP_HEADER_EXTENSION_TRANSPORT_CC_ID, RTP_HEADER_EXTENSION_TRANSPORT_CC_ATTRIBUTE);
		}

		if (composition_time_enabled)
		{
			video_media_desc->AddExtmap(RTP_HEADER_EXTENSION_COMPOSITION_TIME_ID, RTP_HEADER_EXTENSION_COMPOSITION_TIME_ATTRIBUTE);
		}

		std::shared_ptr<PayloadAttr> payload;
		// H264
		payload = std::make_shared<PayloadAttr>();
		payload->SetRtpmap(payload_type_num++, "H264", 90000);
		payload->SetFmtp(ov::String::FormatString("packetization-mode=1;profile-level-id=%x;level-asymmetry-allowed=1",	0x42e01f));
		payload->EnableRtcpFb(PayloadAttr::RtcpFbType::CcmFir, true);
		payload->EnableRtcpFb(PayloadAttr::RtcpFbType::NackPli, true);
		payload->EnableRtcpFb(PayloadAttr::RtcpFbType::TransportCc, true);
		video_media_desc->AddPayload(payload);

		// VP8
		payload = std::make_shared<PayloadAttr>();
		payload->SetRtpmap(payload_type_num++, "VP8", 90000);
		payload->EnableRtcpFb(PayloadAttr::RtcpFbType::CcmFir, true);
		payload->EnableRtcpFb(PayloadAttr::RtcpFbType::NackPli, true);
		
		if (transport_cc_enabled)
		{
			payload->EnableRtcpFb(PayloadAttr::RtcpFbType::TransportCc, true);
		}

		video_media_desc->AddPayload(payload);

		video_media_desc->Update();
		offer_sdp->AddMedia(video_media_desc);

		///////////////////////////////////////
		// Audio Media Description
		///////////////////////////////////////
		auto audio_media_desc = std::make_shared<MediaDescription>();
		audio_media_desc->SetConnection(4, "0.0.0.0");
		audio_media_desc->SetMid(ov::Random::GenerateString(6));
		audio_media_desc->SetMsid(msid, ov::Random::GenerateString(36));
		audio_media_desc->SetSetup(MediaDescription::SetupType::ActPass);
		audio_media_desc->UseDtls(true);
		audio_media_desc->UseRtcpMux(true);
		audio_media_desc->SetDirection(MediaDescription::Direction::RecvOnly);
		audio_media_desc->SetMediaType(MediaDescription::MediaType::Audio);

		if (transport_cc_enabled)
		{
			audio_media_desc->AddExtmap(RTP_HEADER_EXTENSION_TRANSPORT_CC_ID, RTP_HEADER_EXTENSION_TRANSPORT_CC_ATTRIBUTE);
		}

		// OPUS
		payload = std::make_shared<PayloadAttr>();
		payload->SetFmtp("sprop-stereo=1;stereo=1;minptime=10;useinbandfec=1");
		payload->SetRtpmap(payload_type_num++, "OPUS", 48000, "2");
		
		if (transport_cc_enabled)
		{
			payload->EnableRtcpFb(PayloadAttr::RtcpFbType::TransportCc, true);
		}
		audio_media_desc->AddPayload(payload);

		audio_media_desc->Update();
		offer_sdp->AddMedia(audio_media_desc);

		offer_sdp->Update();
		logtt("Offer SDP created : %s", offer_sdp->ToString().CStr());

		return offer_sdp;
	}

	std::shared_ptr<SessionDescription> WebRTCApplication::CreateAnswerSDP(const std::shared_ptr<const SessionDescription> &offer_sdp, const ov::String &local_ufrag, const std::set<IceCandidate> &ice_candidates)
	{
		if(offer_sdp == nullptr)
		{
			return nullptr;
		}

		auto answer_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Answer);
		answer_sdp->SetOrigin("OvenMediaEngine", ov::Random::GenerateUInt32(), 2, "IN", 4, "127.0.0.1");
		answer_sdp->SetTiming(0, 0);
		answer_sdp->SetExtmapAllowMixed(offer_sdp->GetExtmapAllowMixed());
		
		// msid-semantic, only add if offer has msid-semantic
		ov::String msid_semantic;
		if (offer_sdp->GetMsidSemantic().IsEmpty() == false)
		{
			msid_semantic = ov::Random::GenerateString(36);
			answer_sdp->SetMsidSemantic("WMS", msid_semantic);
		}

		auto ice_local_ufrag = local_ufrag;
		auto ice_local_pwd = ov::Random::GenerateString(32);

		for (auto &offer_media_desc : offer_sdp->GetMediaList())
		{
			if (offer_media_desc->GetMediaType() != MediaDescription::MediaType::Video &&
				offer_media_desc->GetMediaType() != MediaDescription::MediaType::Audio)
			{
				logte("Offer SDP has invalid media type - %s", offer_media_desc->GetMediaTypeStr().CStr());
				continue;
			}

			auto answer_media_desc = std::make_shared<MediaDescription>();
			answer_media_desc->SetMediaType(offer_media_desc->GetMediaType());
			answer_media_desc->SetConnection(4, "0.0.0.0");

			// rtcp-mux
			if (offer_media_desc->IsRtcpMux() == false)
			{
				logtw("Offer SDP has no rtcp-mux");
			}
			answer_media_desc->UseRtcpMux(true);

			// recvonly
			answer_media_desc->SetDirection(MediaDescription::Direction::RecvOnly);

			// msid
			if (msid_semantic.IsEmpty() == false)
			{
				answer_media_desc->SetMsid(msid_semantic, ov::Random::GenerateString(36));
			}

			// ice-ufrag, ice-pwd, ice-options, fingerprint
			answer_media_desc->SetIceUfrag(ice_local_ufrag);
			answer_media_desc->SetIcePwd(ice_local_pwd);
			answer_media_desc->SetFingerprint("sha-256", _certificate->GetFingerprint("sha-256"));

			// setup
			if (offer_media_desc->GetSetup() == MediaDescription::SetupType::Passive)
			{
				logte("Offer SDP has invalid setup type - a=setup:passive");
				return nullptr;
			}
			answer_media_desc->SetSetup(MediaDescription::SetupType::Passive);

			// mid
			answer_media_desc->SetMid(offer_media_desc->GetMid().value_or(""));

			// ssrc & cname
			if (offer_media_desc->GetSsrc().has_value())
			{
				answer_media_desc->SetSsrc(offer_media_desc->GetSsrc().value());
			}

			if (offer_media_desc->GetCname().has_value())
			{
				answer_media_desc->SetCname(offer_media_desc->GetCname().value());
			}

			// transport-cc
			uint8_t extmap_id = 0;
			ov::String extmap_attribute;
			if (offer_media_desc->FindExtmapItem("transport-wide-cc", extmap_id, extmap_attribute))
			{
				answer_media_desc->AddExtmap(extmap_id, extmap_attribute);
			}

			// CompositionTime
			if (offer_media_desc->FindExtmapItem("uri:ietf:rtc:rtp-hdrext:video:CompositionTime", extmap_id, extmap_attribute))
			{
				answer_media_desc->AddExtmap(extmap_id, extmap_attribute);
			}

			// MID
			if (offer_media_desc->FindExtmapItem("urn:ietf:params:rtp-hdrext:sdes:mid", extmap_id, extmap_attribute))
			{
				answer_media_desc->AddExtmap(extmap_id, extmap_attribute);
			}

			// RTP-STREAM-ID
			if (offer_media_desc->FindExtmapItem("urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id", extmap_id, extmap_attribute))
			{
				answer_media_desc->AddExtmap(extmap_id, extmap_attribute);
			}

			// a=candidate
			for (const auto &ice_candidate : ice_candidates)
			{
				answer_media_desc->AddIceCandidate(std::make_shared<IceCandidate>(ice_candidate));
			}

			// payloads
			for (auto &offer_payload : offer_media_desc->GetPayloadList())
			{
				if (offer_payload->GetCodec() != PayloadAttr::SupportCodec::H264 &&
					offer_payload->GetCodec() != PayloadAttr::SupportCodec::H265 &&
					offer_payload->GetCodec() != PayloadAttr::SupportCodec::VP8 &&
					offer_payload->GetCodec() != PayloadAttr::SupportCodec::OPUS)
				{
					logti("unsupported codec(%s) has ignored", offer_payload->GetCodecStr().CStr());
					continue;
				}

				auto answer_payload = std::make_shared<PayloadAttr>();
				answer_payload->SetRtpmap(offer_payload->GetId(), offer_payload->GetCodecStr(), offer_payload->GetCodecRate(), offer_payload->GetCodecParams());
				answer_payload->SetFmtp(offer_payload->GetFmtp());

				// rtcp-fb

				// CCM FIR
				if (offer_payload->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::CcmFir))
				{
					answer_payload->EnableRtcpFb(PayloadAttr::RtcpFbType::CcmFir, true);
				}

				// NACK PLI
				if (offer_payload->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::NackPli))
				{
					answer_payload->EnableRtcpFb(PayloadAttr::RtcpFbType::NackPli, true);
				}

				// Transport CC
				if (offer_payload->IsRtcpFbEnabled(PayloadAttr::RtcpFbType::TransportCc))
				{
					answer_payload->EnableRtcpFb(PayloadAttr::RtcpFbType::TransportCc, true);
				}
				
				answer_media_desc->AddPayload(answer_payload);
			}

			// rids
			for (auto &rid : offer_media_desc->GetRidList())
			{
				auto answer_rid = std::make_shared<RidAttr>();
				answer_rid->SetId(rid->GetId());
				answer_rid->SetDirection(rid->GetDirection() == RidAttr::Direction::Send ? RidAttr::Direction::Recv : RidAttr::Direction::Send);
				answer_rid->SetState(rid->GetState());
				for (const auto &restriction : rid->GetRestrictions())
				{
					auto key = restriction.first;
					auto value = restriction.second;

					// remove alternative pt, OME doesn't support it 
					if (key.LowerCaseString() == "pt")
					{
						auto pt_list = value.Split(",");
						value = pt_list[0];
					}

					answer_rid->AddRestriction(key, value);
				}

				answer_media_desc->AddRid(answer_rid, false);
			}

			// simulcast
			for (auto &layer : offer_media_desc->GetSendLayerList())
			{
				auto answer_layer = std::make_shared<SimulcastLayer>();
				answer_layer->SetRidList(layer->GetRidList());
				answer_media_desc->AddRecvLayerToSimulcast(answer_layer);
			}

			for (auto &layer : offer_media_desc->GetRecvLayerList())
			{
				auto answer_layer = std::make_shared<SimulcastLayer>();
				answer_layer->SetRidList(layer->GetRidList());
				answer_media_desc->AddSendLayerToSimulcast(answer_layer);
			}

			answer_media_desc->Update();
			answer_sdp->AddMedia(answer_media_desc);
		}
		
		answer_sdp->Update();

		logti("Answer SDP created : %s", answer_sdp->ToString().CStr());

		return answer_sdp;
	}
}