//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtc_stream.h"

#include <base/info/media_extradata.h>
#include <modules/bitstream/h264/h264_decoder_configuration_record.h>
#include <modules/bitstream/nalu/nal_stream_converter.h>
#include <modules/rtp_rtcp/rtp_header_extension/rtp_header_extension_abs_send_time.h>
#include <modules/rtp_rtcp/rtp_header_extension/rtp_header_extension_framemarking.h>
#include <modules/rtp_rtcp/rtp_header_extension/rtp_header_extension_playout_delay.h>

#include "rtc_application.h"
#include "rtc_common_types.h"
#include "rtc_private.h"
#include "rtc_session.h"

using namespace cmn;

/***************************
 SDP Sample
****************************
v=0
o=OvenMediaEngine 100 2 IN IP4 127.0.0.1
s=-
t=0 0
a=group:BUNDLE ifUNok 8TKNaB
a=group:LS ifUNok 8TKNaB
a=msid-semantic:WMS a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ
a=fingerprint:sha-256 68:BD:A0:DE:31:67:01:AD:14:7F:92:7A:81:3F:01:5E:47:A0:82:72:84:E0:71:38:EF:87:2D:04:7C:0E:5D:61
a=ice-options:trickle
a=ice-ufrag:W3x6EQ
a=ice-pwd:4q1k7P8gwrDoBmbxiNQSH0aUOyCJlT5u
m=video 9 UDP/TLS/RTP/SAVPF 98 99 120 121 122
c=IN IP4 0.0.0.0
a=sendonly
a=mid:ifUNok
a=setup:actpass
a=rtcp-mux
a=msid:a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ aSo7tDegF8TYyC6U2NR9qM5vm1dQrGjZBuVL
a=extmap:1 urn:ietf:params:rtp-hdrext:framemarking
a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
a=rtpmap:98 H264/90000
a=fmtp:98 packetization-mode=1;profile-level-id=42e01f;level-asymmetry-allowed=1
a=rtcp-fb:98 transport-cc
a=rtcp-fb:98 nack
a=rtpmap:99 rtx/90000
a=fmtp:99 apt=98
a=rtpmap:120 red/90000
a=rtpmap:121 rtx/90000
a=fmtp:121 apt=120
a=rtpmap:122 ulpfec/90000
a=ssrc-group:FID 649759348 536933936
a=ssrc:649759348 cname:YQ5Ss9POwjA6ypoJ
a=ssrc:649759348 msid:a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ aSo7tDegF8TYyC6U2NR9qM5vm1dQrGjZBuVL
a=ssrc:649759348 mslabel:a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ
a=ssrc:649759348 label:aSo7tDegF8TYyC6U2NR9qM5vm1dQrGjZBuVL
a=ssrc:536933936 cname:YQ5Ss9POwjA6ypoJ
a=ssrc:536933936 msid:a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ aSo7tDegF8TYyC6U2NR9qM5vm1dQrGjZBuVL
a=ssrc:536933936 mslabel:a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ
a=ssrc:536933936 label:aSo7tDegF8TYyC6U2NR9qM5vm1dQrGjZBuVL
m=audio 9 UDP/TLS/RTP/SAVPF 110
c=IN IP4 0.0.0.0
a=sendonly
a=mid:8TKNaB
a=setup:actpass
a=rtcp-mux
a=msid:a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ U59mpow1QKCqzIT80tekN4yDxsjM62vJbAVB
a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
a=rtpmap:110 OPUS/48000/2
a=fmtp:110 sprop-stereo=1;stereo=1;minptime=10;useinbandfec=1
a=rtcp-fb:110 transport-cc
a=ssrc:1273183178 cname:YQ5Ss9POwjA6ypoJ
a=ssrc:1273183178 msid:a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ U59mpow1QKCqzIT80tekN4yDxsjM62vJbAVB
a=ssrc:1273183178 mslabel:a6Dt3TF7zAXnsj0SECUobPQNRvhH2r5YqieJ
a=ssrc:1273183178 label:U59mpow1QKCqzIT80tekN4yDxsjM62vJbAVB
****************************/

std::shared_ptr<RtcStream> RtcStream::Create(const std::shared_ptr<pub::Application> application,
											 const info::Stream &info,
											 uint32_t worker_count)
{
	auto stream = std::make_shared<RtcStream>(application, info, worker_count);
	return stream;
}

RtcStream::RtcStream(const std::shared_ptr<pub::Application> application,
					 const info::Stream &info,
					 uint32_t worker_count)
	: Stream(application, info),
	  _worker_count(worker_count)
{
	_certificate	= application->GetSharedPtrAs<RtcApplication>()->GetCertificate();
	_vp8_picture_id = 0x8000;  // 1 {000 0000 0000 0000} 1 is marker for 15 bit length
}

RtcStream::~RtcStream()
{
	logtt("RtcStream(%d) has been terminated finally", GetId());
	Stop();
}

std::shared_ptr<const pub::Stream::DefaultPlaylistInfo> RtcStream::GetDefaultPlaylistInfo() const
{
	static auto info = std::make_shared<pub::Stream::DefaultPlaylistInfo>(
		"webrtc_default",
		"webrtc_default",
		"webrtc_default");

	return info;
}

bool RtcStream::Start()
{
	if (GetState() != State::CREATED)
	{
		return false;
	}

	if (!CreateStreamWorker(_worker_count))
	{
		return false;
	}

	auto webrtc_config	   = GetApplicationInfo().GetConfig().GetPublishers().GetWebrtcPublisher();

	_rtx_enabled		   = webrtc_config.IsRtxEnabled();
	_ulpfec_enabled		   = webrtc_config.IsUlpfecEnalbed();
	_jitter_buffer_enabled = webrtc_config.IsJitterBufferEnabled();

	auto playoutDelay	   = webrtc_config.GetPlayoutDelay(&_playout_delay_enabled);
	_playout_delay_min	   = playoutDelay.GetMin();
	_playout_delay_max	   = playoutDelay.GetMax();

	if (webrtc_config.GetBandwidthEstimationType() == WebRtcBandwidthEstimationType::TransportCc)
	{
		_transport_cc_enabled = true;
	}
	else if (webrtc_config.GetBandwidthEstimationType() == WebRtcBandwidthEstimationType::REMB)
	{
		_remb_enabled = true;
	}

	// MSID
	_msid										   = ov::Random::GenerateString(36);
	_cname										   = ov::Random::GenerateString(16);

	// SSRC
	_video_ssrc									   = ov::Random::GenerateUInt32();
	_video_rtx_ssrc								   = ov::Random::GenerateUInt32();
	_audio_ssrc									   = ov::Random::GenerateUInt32();

	std::shared_ptr<MediaTrack> _first_video_track = nullptr;
	std::shared_ptr<MediaTrack> _first_audio_track = nullptr;

	// Create Packetizer
	for (auto &[track_id, track] : GetTracks())
	{
		if (track->GetMediaType() != cmn::MediaType::Video && track->GetMediaType() != cmn::MediaType::Audio)
		{
			continue;
		}

		if (IsSupportedCodec(track->GetCodecId()) == false)
		{
			logti("RtcStream(%s/%s) - Ignore unsupported codec(%s)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), cmn::GetCodecIdString(track->GetCodecId()));
			continue;
		}

		// For default Playlist : the first tracks for each supported codec
		if (_first_video_track == nullptr && track->GetMediaType() == cmn::MediaType::Video)
		{
			_first_video_track = track;
		}
		else if (_first_audio_track == nullptr && track->GetMediaType() == cmn::MediaType::Audio)
		{
			_first_audio_track = track;
		}

		AddPacketizer(track);

		if (_rtx_enabled == true)
		{
			AddRtpHistory(track);
		}

		if (_jitter_buffer_enabled == true)
		{
			_jitter_buffer_delay.CreateJitterBuffer(track->GetId(), track->GetTimeBase().GetDen());
		}
	}

	if (webrtc_config.ShouldCreateDefaultPlaylist() == true)
	{
		// Create Default Playlist for no file name (ws://domain/app/stream)
		auto default_playlist_info = GetDefaultPlaylistInfo();
		OV_ASSERT2(default_playlist_info != nullptr);

		_default_playlist_name = default_playlist_info->file_name;

		auto default_playlist  = GetPlaylist(_default_playlist_name);
		if (default_playlist == nullptr)
		{
			auto playlist  = std::make_shared<info::Playlist>(default_playlist_info->name, _default_playlist_name, true);
			auto rendition = std::make_shared<info::Rendition>("default", _first_video_track ? _first_video_track->GetVariantName() : "", _first_audio_track ? _first_audio_track->GetVariantName() : "");

			playlist->AddRendition(rendition);
			playlist->SetWebRtcAutoAbr(false);

			AddPlaylist(playlist);
		}

		auto rtc_master_playlist = CreateRtcMasterPlaylist(_default_playlist_name);

		// lock
		std::lock_guard<std::shared_mutex> lock(_rtc_master_playlist_map_lock);
		_rtc_master_playlist_map[_default_playlist_name] = rtc_master_playlist;
	}
	else
	{
		logti("RtcStream(%s/%s) - Default playlist creation is disabled", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
		if (GetPlaylists().size() == 0)
		{
			logtw("RtcStream(%s/%s) - There is no playlist, WebRTC will not work for this stream.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			Stop();	 // Release resources
			return false;
		}
	}

	logti("WebRTC Stream has been created : %s/%u\nRtx(%s) Ulpfec(%s) JitterBuffer(%s) PlayoutDelay(%s min:%d max: %d)",
		  GetName().CStr(), GetId(),
		  ov::Converter::ToString(_rtx_enabled).CStr(),
		  ov::Converter::ToString(_ulpfec_enabled).CStr(),
		  ov::Converter::ToString(_jitter_buffer_enabled).CStr(),
		  ov::Converter::ToString(_playout_delay_enabled).CStr(),
		  _playout_delay_min, _playout_delay_max);

	return Stream::Start();
}

bool RtcStream::Stop()
{
	if (GetState() != State::STARTED)
	{
		return false;
	}

	std::lock_guard<std::shared_mutex> lock(_packetizers_lock);
	_packetizers.clear();

	return Stream::Stop();
}

bool RtcStream::Update(const std::shared_ptr<info::Stream> &info)
{
	SetMsid(info->GetMsid());

	//TODO(Getroot): check if the track has changed and re-create the SDP.

	return Stream::Update(info);
}

bool RtcStream::IsSupportedCodec(cmn::MediaCodecId codec_id)
{
	switch (codec_id)
	{
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::H265:
		case cmn::MediaCodecId::Vp8:
		case cmn::MediaCodecId::Opus:
			return true;
		default:
			return false;
	}

	return false;
}

std::shared_ptr<const RtcPlaylist> RtcStream::GetRtcPlaylist(const ov::String &file_name, cmn::MediaCodecId video_codec_id, cmn::MediaCodecId audio_codec_id)
{
	auto master_rtc_playlist = GetRtcMasterPlaylist(file_name);
	if (master_rtc_playlist == nullptr)
	{
		return nullptr;
	}

	return master_rtc_playlist->GetPlaylist(video_codec_id, audio_codec_id);
}

std::shared_ptr<const RtcMasterPlaylist> RtcStream::GetRtcMasterPlaylist(const ov::String &file_name)
{
	if (file_name.IsEmpty())
	{
		return GetRtcMasterPlaylist(_default_playlist_name);
	}

	std::shared_ptr<const RtcMasterPlaylist> rtc_master_playlist;

	//lock
	std::shared_lock<std::shared_mutex> lock(_rtc_master_playlist_map_lock);
	auto it = _rtc_master_playlist_map.find(file_name);
	if (it != _rtc_master_playlist_map.end())
	{
		rtc_master_playlist = it->second;
	}
	lock.unlock();

	if (rtc_master_playlist == nullptr)
	{
		rtc_master_playlist = CreateRtcMasterPlaylist(file_name);
		if (rtc_master_playlist == nullptr)
		{
			return nullptr;
		}

		// lock
		std::lock_guard<std::shared_mutex> lock(_rtc_master_playlist_map_lock);
		_rtc_master_playlist_map[file_name] = rtc_master_playlist;
	}

	return rtc_master_playlist;
}

std::shared_ptr<RtcMasterPlaylist> RtcStream::CreateRtcMasterPlaylist(const ov::String &file_name)
{
	// Get info::Playlist
	auto playlist = GetPlaylist(file_name);
	if (playlist == nullptr)
	{
		return nullptr;
	}

	auto rtc_master_playlist = std::make_shared<RtcMasterPlaylist>(file_name, playlist->GetName());
	rtc_master_playlist->SetWebRtcAutoAbr(playlist->IsWebRtcAutoAbr());

	for (const auto &rendition : playlist->GetRenditionList())
	{
		auto video_index_hint = rendition->GetVideoIndexHint();
		if (video_index_hint < 0)
		{
			video_index_hint = 0;
		}

		auto audio_index_hint = rendition->GetAudioIndexHint();
		if (audio_index_hint < 0)
		{
			audio_index_hint = 0;
		}

		auto video_track = GetTrackByVariant(rendition->GetVideoVariantName(), video_index_hint);
		auto audio_track = GetTrackByVariant(rendition->GetAudioVariantName(), audio_index_hint);

		if (video_track == nullptr && audio_track == nullptr)
		{
			logtw("RtcStream(%s/%s) - Exclude the rendition(%s) from the %s playlist due to no video (index:%d) and audio track (index:%d)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), rendition->GetName().CStr(), playlist->GetFileName().CStr(), video_index_hint, audio_index_hint);
			continue;
		}

		rtc_master_playlist->AddRendition(std::make_shared<RtcRendition>(rendition->GetName(), video_track, audio_track));
	}

	return rtc_master_playlist;
}

std::shared_ptr<const SessionDescription> RtcStream::GetSessionDescription(const ov::String &file_name)
{
	if (GetState() != State::STARTED)
	{
		return nullptr;
	}

	std::shared_ptr<const SessionDescription> offer_sdp;
	//lock
	std::shared_lock<std::shared_mutex> lock(_offer_sdp_lock);
	auto it = _offer_sdp_map.find(file_name);
	if (it != _offer_sdp_map.end())
	{
		offer_sdp = it->second;
	}
	lock.unlock();

	if (offer_sdp == nullptr)
	{
		offer_sdp = CreateSessionDescription(file_name);
		if (offer_sdp == nullptr)
		{
			return nullptr;
		}

		// lock
		std::lock_guard<std::shared_mutex> lock(_offer_sdp_lock);
		_offer_sdp_map[file_name] = offer_sdp;
	}

	return offer_sdp;
}

std::shared_ptr<SessionDescription> RtcStream::CreateSessionDescription(const ov::String &file_name)
{
	auto rtc_master_playlist = GetRtcMasterPlaylist(file_name);
	if (rtc_master_playlist == nullptr)
	{
		return nullptr;
	}

	auto offer_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer);

	offer_sdp->SetOrigin("OvenMediaEngine", ov::Random::GenerateUInt32(), 2, "IN", 4, "127.0.0.1");
	offer_sdp->SetExtmapAllowMixed(true);
	offer_sdp->SetTiming(0, 0);
	offer_sdp->SetIceOption("trickle");
	offer_sdp->SetIceUfrag(ov::Random::GenerateString(8));
	offer_sdp->SetIcePwd(ov::Random::GenerateString(32));

	offer_sdp->SetMsidSemantic("WMS", _msid);
	offer_sdp->SetFingerprint("sha-256", _certificate->GetFingerprint("sha-256"));

	std::shared_ptr<MediaDescription> video_media_desc = nullptr;
	std::shared_ptr<MediaDescription> audio_media_desc = nullptr;

	// Add payloads for each codec in the playlist
	for (const auto &[codec_id, track] : rtc_master_playlist->GetPayloadTrackMap())
	{
		auto payload_type = PayloadTypeFromCodecId(track->GetCodecId());
		// Unsupported codec
		if (payload_type == 0)
		{
			continue;
		}

		if (track->GetMediaType() == MediaType::Video && video_media_desc == nullptr)
		{
			video_media_desc = MakeVideoDescription();
			offer_sdp->AddMedia(video_media_desc);
		}
		else if (track->GetMediaType() == MediaType::Audio && audio_media_desc == nullptr)
		{
			audio_media_desc = MakeAudioDescription();
			offer_sdp->AddMedia(audio_media_desc);
		}

		auto &media_desc = track->GetMediaType() == MediaType::Video ? video_media_desc : audio_media_desc;

		// The first track is added for each supported codec.
		if (media_desc->GetPayload(payload_type) != nullptr)
		{
			continue;
		}

		auto payload = MakePayloadAttr(track);
		media_desc->AddPayload(payload);

		// Add RTX
		if (track->GetMediaType() == cmn::MediaType::Video && _rtx_enabled == true)
		{
			payload->EnableRtcpFb(PayloadAttr::RtcpFbType::Nack, true);
			auto rtx_payload = MakeRtxPayloadAttr(track);
			media_desc->AddPayload(rtx_payload);
		}

		media_desc->Update();
	}

	// ULPFEC payload
	if (video_media_desc && _ulpfec_enabled == true)
	{
		// RED & ULPFEC
		auto red_payload = std::make_shared<PayloadAttr>();
		red_payload->SetRtpmap(static_cast<uint8_t>(FixedRtcPayloadType::RED_PAYLOAD_TYPE), "red", 90000);
		video_media_desc->AddPayload(red_payload);

		// For RTX
		if (_rtx_enabled == true)
		{
			// RTX for RED
			auto rtx_payload = std::make_shared<PayloadAttr>();
			rtx_payload->SetRtpmap(static_cast<uint8_t>(FixedRtcPayloadType::RED_RTX_PAYLOAD_TYPE), "rtx", 90000);
			rtx_payload->SetFmtp(ov::String::FormatString("apt=%d", static_cast<uint8_t>(FixedRtcPayloadType::RED_PAYLOAD_TYPE)));
			video_media_desc->AddPayload(rtx_payload);
		}

		// ULPFEC
		auto ulpfec_payload = std::make_shared<PayloadAttr>();
		ulpfec_payload->SetRtpmap(static_cast<uint8_t>(FixedRtcPayloadType::ULPFEC_PAYLOAD_TYPE), "ulpfec", 90000);
		video_media_desc->AddPayload(ulpfec_payload);

		video_media_desc->Update();
	}

	offer_sdp->Update();

	return offer_sdp;
}

std::shared_ptr<MediaDescription> RtcStream::MakeVideoDescription() const
{
	auto video_media_desc = std::make_shared<MediaDescription>();
	video_media_desc->SetConnection(4, "0.0.0.0");
	video_media_desc->SetMid(ov::Random::GenerateString(6));
	video_media_desc->SetMsid(_msid, ov::Random::GenerateString(36));
	/*
	https://tools.ietf.org/html/rfc5763#section-5
	
	The endpoint MUST use the setup attribute defined in [RFC4145].
	The endpoint that is the offerer MUST use the setup attribute
	value of setup:actpass and be prepared to receive a client_hello
	before it receives the answer.
	*/
	video_media_desc->SetSetup(MediaDescription::SetupType::ActPass);
	video_media_desc->UseDtls(true);
	video_media_desc->UseRtcpMux(true);
	video_media_desc->UseRtcpRsize(true);
	video_media_desc->SetDirection(MediaDescription::Direction::SendOnly);
	video_media_desc->SetMediaType(MediaDescription::MediaType::Video);
	// Cname
	video_media_desc->SetCname(_cname);
	// Media SSRC
	video_media_desc->SetSsrc(_video_ssrc);
	// RTX SSRC
	if (_rtx_enabled == true)
	{
		video_media_desc->SetRtxSsrc(_video_rtx_ssrc);
	}
	video_media_desc->AddExtmap(RTP_HEADER_EXTENSION_FRAMEMARKING_ID, RTP_HEADER_EXTENSION_FRAMEMARKING_ATTRIBUTE);

	// Experimental Code
	if (_playout_delay_enabled == true)
	{
		video_media_desc->AddExtmap(RTP_HEADER_EXTENSION_PLAYOUT_DELAY_ID, RTP_HEADER_EXTENSION_PLAYOUT_DELAY_ATTRIBUTE);
	}

	if (_transport_cc_enabled == true)
	{
		video_media_desc->AddExtmap(RTP_HEADER_EXTENSION_TRANSPORT_CC_ID, RTP_HEADER_EXTENSION_TRANSPORT_CC_ATTRIBUTE);
	}

	if (_remb_enabled == true)
	{
		video_media_desc->AddExtmap(RTP_HEADER_EXTENSION_ABS_SEND_TIME_ID, RTP_HEADER_EXTENSION_ABS_SEND_TIME_ATTRIBUTE);
	}

	return video_media_desc;
}

std::shared_ptr<MediaDescription> RtcStream::MakeAudioDescription() const
{
	auto audio_media_desc = std::make_shared<MediaDescription>();

	audio_media_desc->SetConnection(4, "0.0.0.0");
	// TODO(dimiden): Need to prevent duplication
	audio_media_desc->SetMid(ov::Random::GenerateString(6));
	audio_media_desc->SetMsid(_msid, ov::Random::GenerateString(36));
	audio_media_desc->SetSetup(MediaDescription::SetupType::ActPass);
	audio_media_desc->UseDtls(true);
	audio_media_desc->UseRtcpMux(true);
	audio_media_desc->UseRtcpRsize(true);
	audio_media_desc->SetDirection(MediaDescription::Direction::SendOnly);
	audio_media_desc->SetMediaType(MediaDescription::MediaType::Audio);
	// Cname
	audio_media_desc->SetCname(_cname);
	// Media SSRC
	audio_media_desc->SetSsrc(_audio_ssrc);

	if (_transport_cc_enabled == true)
	{
		audio_media_desc->AddExtmap(RTP_HEADER_EXTENSION_TRANSPORT_CC_ID, RTP_HEADER_EXTENSION_TRANSPORT_CC_ATTRIBUTE);
	}

	if (_remb_enabled == true)
	{
		audio_media_desc->AddExtmap(RTP_HEADER_EXTENSION_ABS_SEND_TIME_ID, RTP_HEADER_EXTENSION_ABS_SEND_TIME_ATTRIBUTE);
	}

	return audio_media_desc;
}

std::shared_ptr<PayloadAttr> RtcStream::MakePayloadAttr(const std::shared_ptr<const MediaTrack> &track) const
{
	auto payload = std::make_shared<PayloadAttr>();

	switch (track->GetCodecId())
	{
		case MediaCodecId::Vp8:
			payload->SetRtpmap(PayloadTypeFromCodecId(track->GetCodecId()), "VP8", 90000);
			break;
		case MediaCodecId::H265:
			payload->SetRtpmap(PayloadTypeFromCodecId(track->GetCodecId()), "H265", 90000);
			break;
		case MediaCodecId::H264:
			payload->SetRtpmap(PayloadTypeFromCodecId(track->GetCodecId()), "H264", 90000);

			{
				auto config = std::static_pointer_cast<AVCDecoderConfigurationRecord>(track->GetDecoderConfigurationRecord());

				// profile-level-id
				ov::String profile_string;

				profile_string.Format("%02x%02x%02x",
									  config->ProfileIndication(),
									  config->Compatibility(),
									  config->LevelIndication());

				//(Getroot's Note) The software decoder of Firefox or Chrome cannot play when 64001f (High, 3.1) stream is input.
				// However, when I put the fake information of 42e01f in FMTP, I confirmed that both Firefox and Chrome play well (high profile, but stream without B-Frame).
				// I thought it would be better to put 42e01f in fmtp than put the correct value, so I decided to put fake information.
				profile_string = "42e01f";

				payload->SetFmtp(ov::String::FormatString(
					// NonInterleaved => packetization-mode=1
					"packetization-mode=1;profile-level-id=%s;level-asymmetry-allowed=1",
					profile_string.CStr()));
			}

			break;
		case MediaCodecId::Opus:
			payload->SetRtpmap(PayloadTypeFromCodecId(track->GetCodecId()), "OPUS", static_cast<uint32_t>(track->GetSample().GetRateNum()), "2");

			// Enable inband-fec
			// a=fmtp:111 maxplaybackrate=16000; useinbandfec=1; maxaveragebitrate=20000
			if (track->GetChannel().GetLayout() == cmn::AudioChannel::Layout::LayoutStereo)
			{
				payload->SetFmtp("sprop-stereo=1;stereo=1;minptime=10;useinbandfec=1");
			}
			else
			{
				payload->SetFmtp("sprop-stereo=0;stereo=0;minptime=10;useinbandfec=1");
			}
			break;
		default:
			logti("Unsupported codec(%s/%s) is being input from media track",
				  cmn::GetMediaTypeString(track->GetMediaType()),
				  cmn::GetCodecIdString(track->GetCodecId()));
			return nullptr;
	}

	if (_transport_cc_enabled == true)
	{
		payload->EnableRtcpFb(PayloadAttr::RtcpFbType::TransportCc, true);
	}

	if (track->GetMediaType() == cmn::MediaType::Video && _remb_enabled == true)
	{
		payload->EnableRtcpFb(PayloadAttr::RtcpFbType::GoogRemb, true);
	}

	return payload;
}

std::shared_ptr<PayloadAttr> RtcStream::MakeRtxPayloadAttr(const std::shared_ptr<const MediaTrack> &track) const
{
	uint8_t source_payload_type = PayloadTypeFromCodecId(track->GetCodecId());
	uint8_t rtx_payload_type	= RtxPayloadTypeFromCodecId(track->GetCodecId());

	if (source_payload_type == 0 || rtx_payload_type == 0)
	{
		return nullptr;
	}

	auto rtx_payload = std::make_shared<PayloadAttr>();
	rtx_payload->SetRtpmap(rtx_payload_type, "rtx", 90000);
	rtx_payload->SetFmtp(ov::String::FormatString("apt=%d", source_payload_type));

	return rtx_payload;
}

bool RtcStream::OnRtpPacketized(std::shared_ptr<RtpPacket> packet)
{
	auto stream_packet = std::make_any<std::shared_ptr<RtpPacket>>(packet);
	BroadcastPacket(stream_packet);

	if (_rtx_enabled == true)
	{
		// Store for retransmission
		auto history = GetHistory(packet->GetTrackId(), packet->PayloadType());
		if (history != nullptr)
		{
			history->StoreRtpPacket(packet);
		}
	}

	return true;
}

void RtcStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (_jitter_buffer_enabled)
	{
		PushToJitterBuffer(media_packet);
	}
	else
	{
		PacketizeVideoFrame(media_packet);
	}
}

void RtcStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (_jitter_buffer_enabled)
	{
		PushToJitterBuffer(media_packet);
	}
	else
	{
		PacketizeAudioFrame(media_packet);
	}
}

void RtcStream::PushToJitterBuffer(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (GetState() != State::STARTED)
	{
		return;
	}

	_jitter_buffer_delay.PushMediaPacket(media_packet);

	while (auto media_packet = _jitter_buffer_delay.PopNextMediaPacket())
	{
		if (media_packet->GetMediaType() == cmn::MediaType::Video)
		{
			PacketizeVideoFrame(media_packet);
		}
		else if (media_packet->GetMediaType() == cmn::MediaType::Audio)
		{
			PacketizeAudioFrame(media_packet);
		}
	}
}

void RtcStream::PacketizeVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	auto media_track = GetTrack(media_packet->GetTrackId());
	if (media_track == nullptr)
	{
		logtw("RtcStream(%s/%s) - MediaTrack(%u) is not found", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), media_packet->GetTrackId());
		return;
	}

	// Create RTP Video Header
	CodecSpecificInfo codec_info;
	RTPVideoHeader rtp_video_header;

	codec_info.codec_type = media_track->GetCodecId();

	if (codec_info.codec_type == MediaCodecId::Vp8)
	{
		// Structure for future expansion.
		// In the future, when OME uses codec-specific features, certain information is obtained from media_packet.
		codec_info.codec_specific.vp8 = CodecSpecificInfoVp8();
	}
	else if (codec_info.codec_type == MediaCodecId::H264 ||
			 codec_info.codec_type == MediaCodecId::H265)
	{
		codec_info.codec_specific.h26X = CodecSpecificInfoH26X();
	}

	memset(&rtp_video_header, 0, sizeof(RTPVideoHeader));

	MakeRtpVideoHeader(&codec_info, &rtp_video_header);

	// RTP Packetizing
	auto packetizer = GetPacketizer(media_track->GetId());
	if (packetizer == nullptr)
	{
		return;
	}

	auto frame_type	   = (media_packet->GetFlag() == MediaPacketFlag::Key) ? FrameType::VideoFrameKey : FrameType::VideoFrameDelta;
	// video timescale is always 90000hz in WebRTC
	auto timestamp	   = ((double)media_packet->GetPts() * media_track->GetTimeBase().GetExpr() * 90000);
	auto ntp_timestamp = ov::Converter::SecondsToNtpTs((double)media_packet->GetPts() * media_track->GetTimeBase().GetExpr());
	auto data		   = media_packet->GetData();
	auto fragmentation = media_packet->GetFragHeader();

	packetizer->Packetize(frame_type,
						  timestamp,
						  ntp_timestamp,
						  data->GetDataAs<uint8_t>(),
						  data->GetLength(),
						  fragmentation,
						  &rtp_video_header);
}

void RtcStream::PacketizeAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	auto media_track = GetTrack(media_packet->GetTrackId());
	if (media_track == nullptr)
	{
		logtw("RtcStream(%s/%s) - MediaTrack(%u) is not found", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), media_packet->GetTrackId());
		return;
	}

	// RTP Packetizing
	auto packetizer = GetPacketizer(media_track->GetId());
	if (packetizer == nullptr)
	{
		return;
	}

	auto frame_type	   = (media_packet->GetFlag() == MediaPacketFlag::Key) ? FrameType::AudioFrameKey : FrameType::AudioFrameDelta;
	auto timestamp	   = media_packet->GetPts();
	auto ntp_timestamp = ov::Converter::SecondsToNtpTs((double)media_packet->GetPts() * media_track->GetTimeBase().GetExpr());
	auto data		   = media_packet->GetData();
	auto fragmentation = media_packet->GetFragHeader();

	packetizer->Packetize(frame_type,
						  timestamp,
						  ntp_timestamp,
						  data->GetDataAs<uint8_t>(),
						  data->GetLength(),
						  fragmentation,
						  nullptr);
}

uint16_t RtcStream::AllocateVP8PictureID()
{
	_vp8_picture_id++;

	// PictureID is 7 bit or 15 bit. We use only 15 bit.
	if (_vp8_picture_id == 0)
	{
		// 1{000 0000 0000 0000} is initial number. (first bit means to use 15 bit size)
		_vp8_picture_id = 0x8000;
	}

	return _vp8_picture_id;
}

void RtcStream::MakeRtpVideoHeader(const CodecSpecificInfo *info, RTPVideoHeader *rtp_video_header)
{
	switch (info->codec_type)
	{
		case cmn::MediaCodecId::Vp8:
			rtp_video_header->codec = cmn::MediaCodecId::Vp8;
			rtp_video_header->codec_header.vp8.InitRTPVideoHeaderVP8();
			// With Ulpfec, picture id is needed.
			rtp_video_header->codec_header.vp8.picture_id	 = AllocateVP8PictureID();
			rtp_video_header->codec_header.vp8.non_reference = info->codec_specific.vp8.non_reference;
			rtp_video_header->codec_header.vp8.temporal_idx	 = info->codec_specific.vp8.temporal_idx;
			rtp_video_header->codec_header.vp8.layer_sync	 = info->codec_specific.vp8.layer_sync;
			rtp_video_header->codec_header.vp8.tl0_pic_idx	 = info->codec_specific.vp8.tl0_pic_idx;
			rtp_video_header->codec_header.vp8.key_idx		 = info->codec_specific.vp8.key_idx;
			rtp_video_header->simulcast_idx					 = info->codec_specific.vp8.simulcast_idx;
			return;
		case cmn::MediaCodecId::H264:
			rtp_video_header->codec								   = cmn::MediaCodecId::H264;
			rtp_video_header->codec_header.h26X.packetization_mode = info->codec_specific.h26X.packetization_mode;
			rtp_video_header->simulcast_idx						   = info->codec_specific.h26X.simulcast_idx;
			return;

		case cmn::MediaCodecId::H265:
			rtp_video_header->codec								   = cmn::MediaCodecId::H265;
			rtp_video_header->codec_header.h26X.packetization_mode = info->codec_specific.h26X.packetization_mode;
			rtp_video_header->simulcast_idx						   = info->codec_specific.h26X.simulcast_idx;
			return;
		default:
			break;
	}
}

uint32_t RtcStream::GetSsrc(cmn::MediaType media_type)
{
	uint32_t ssrc = 0;

	if (media_type == cmn::MediaType::Video)
	{
		ssrc = _video_ssrc;
	}
	else if (media_type == cmn::MediaType::Audio)
	{
		ssrc = _audio_ssrc;
	}

	return ssrc;
}

void RtcStream::AddPacketizer(const std::shared_ptr<const MediaTrack> &track)
{
	uint32_t ssrc		 = GetSsrc(track->GetMediaType());
	uint8_t payload_type = PayloadTypeFromCodecId(track->GetCodecId());

	if (ssrc == 0 || payload_type == 0)
	{
		return;
	}

	logtt("Add Packetizer : codec(%u) id(%u) pt(%d) ssrc(%u)", track->GetCodecId(), track->GetId(), payload_type, ssrc);

	auto packetizer = std::make_shared<RtpPacketizer>(RtpPacketizerInterface::GetSharedPtr());
	packetizer->SetCodec(track->GetCodecId());
	packetizer->SetPayloadType(payload_type);
	packetizer->SetTrackId(track->GetId());
	packetizer->SetSSRC(ssrc);

	if (_transport_cc_enabled == true)
	{
		// Transport-wide-cc-extensions is default
		packetizer->EnableTransportCc(0);
	}

	if (_remb_enabled == true)
	{
		packetizer->EnableAbsSendTime();
	}

	if (_ulpfec_enabled == true)
	{
		packetizer->SetUlpfec(static_cast<uint8_t>(FixedRtcPayloadType::RED_PAYLOAD_TYPE), static_cast<uint8_t>(FixedRtcPayloadType::ULPFEC_PAYLOAD_TYPE));
	}

	// Experimental : PlayoutDelay extension
	if (_playout_delay_enabled == true)
	{
		packetizer->SetPlayoutDelay(_playout_delay_min, _playout_delay_max);
	}

	std::lock_guard<std::shared_mutex> lock(_packetizers_lock);
	_packetizers[track->GetId()] = packetizer;
}

std::shared_ptr<RtpPacketizer> RtcStream::GetPacketizer(uint32_t id)
{
	std::shared_lock<std::shared_mutex> lock(_packetizers_lock);
	if (!_packetizers.count(id))
	{
		return nullptr;
	}

	return _packetizers[id];
}

ov::String RtcStream::GetRtpHistoryKey(uint32_t track_id, uint8_t payload_type)
{
	return ov::String::FormatString("%u:%u", track_id, payload_type);
}

void RtcStream::AddRtpHistory(const std::shared_ptr<const MediaTrack> &track)
{
	auto origin_payload_type = PayloadTypeFromCodecId(track->GetCodecId());
	auto rtx_payload_type	 = RtxPayloadTypeFromCodecId(track->GetCodecId());

	if (origin_payload_type == 0 || rtx_payload_type == 0)
	{
		return;
	}

	auto history															= std::make_shared<RtpHistory>(origin_payload_type, rtx_payload_type, _video_rtx_ssrc, MAX_RTP_RECORDS);
	_rtp_history_map[GetRtpHistoryKey(track->GetId(), origin_payload_type)] = history;

	if (_ulpfec_enabled == true)
	{
		auto red_pt												   = static_cast<uint8_t>(FixedRtcPayloadType::RED_PAYLOAD_TYPE);
		auto red_rtx_pt											   = static_cast<uint8_t>(FixedRtcPayloadType::RED_RTX_PAYLOAD_TYPE);
		auto red_history										   = std::make_shared<RtpHistory>(red_pt, red_rtx_pt, _video_rtx_ssrc, MAX_RTP_RECORDS);

		_rtp_history_map[GetRtpHistoryKey(track->GetId(), red_pt)] = red_history;
	}
}

std::shared_ptr<RtpHistory> RtcStream::GetHistory(uint32_t track_id, uint8_t origin_payload_type)
{
	auto key = GetRtpHistoryKey(track_id, origin_payload_type);

	if (_rtp_history_map.find(key) == _rtp_history_map.end())
	{
		return nullptr;
	}

	return _rtp_history_map[key];
}

std::shared_ptr<RtxRtpPacket> RtcStream::GetRtxRtpPacket(uint32_t track_id, uint8_t origin_payload_type, uint16_t origin_sequence_number)
{
	if (GetState() != State::STARTED)
	{
		return nullptr;
	}

	auto history = GetHistory(track_id, origin_payload_type);
	if (history == nullptr)
	{
		return nullptr;
	}

	return history->GetRtxRtpPacket(origin_sequence_number);
}