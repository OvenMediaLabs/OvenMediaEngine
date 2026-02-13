//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./rtmp_stream_v2.h"

#include <base/info/application.h>
#include <base/info/media_extradata.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/provider/push_provider/application.h>
#include <base/provider/push_provider/provider.h>
#include <modules/bitstream/h264/h264_decoder_configuration_record.h>
#include <modules/rtmp_v2/rtmp.h>
#include <orchestrator/orchestrator.h>

#include "./rtmp_provider_private.h"
#include "./tracks/rtmp_track.h"

namespace pvd::rtmp
{
	std::shared_ptr<RtmpStreamV2> RtmpStreamV2::Create(StreamSourceType source_type, uint32_t client_id, const std::shared_ptr<ov::Socket> &client_socket, const std::shared_ptr<PushProvider> &provider)
	{
		auto stream = std::make_shared<RtmpStreamV2>(source_type, client_id, client_socket, provider);

		stream->Start();

		return stream;
	}

	RtmpStreamV2::RtmpStreamV2(StreamSourceType source_type, uint32_t client_id, std::shared_ptr<ov::Socket> client_socket, const std::shared_ptr<PushProvider> &provider)
		: PushStream(source_type, client_id, provider),

		  _handshake_handler(this),
		  _chunk_handler(this),

		  _remote(client_socket)
	{
		logat("Stream has been created");

		SetMediaSource(_remote->GetRemoteAddressAsUrl());
	}

	RtmpStreamV2::~RtmpStreamV2()
	{
		logat("Stream has been terminated finally");
	}

	bool RtmpStreamV2::Start()
	{
		SetState(Stream::State::PLAYING);

		return PushStream::Start();
	}

	bool RtmpStreamV2::Stop()
	{
		if (GetState() == Stream::State::STOPPED)
		{
			return true;
		}

		// Send Close to Admission Webhooks
		auto requested_url = GetRequestedUrl();
		auto final_url	   = GetFinalUrl();

		if (_remote != nullptr)
		{
			if ((requested_url != nullptr) && (final_url != nullptr))
			{
				auto request_info = std::make_shared<ac::RequestInfo>(requested_url, final_url, _remote, nullptr);
				GetProvider()->SendCloseAdmissionWebhooks(request_info);
			}

			// the return check is not necessary
			if (_remote->GetState() == ov::SocketState::Connected)
			{
				_remote->Close();
			}
		}

		_is_post_published = false;

		return PushStream::Stop();
	}

	bool RtmpStreamV2::CheckStreamExpired() const
	{
		return ((_stream_expired_msec != 0) && (_stream_expired_msec < ov::Clock::NowMSec()));
	}

	bool RtmpStreamV2::OnDataReceived(const std::shared_ptr<const ov::Data> &data)
	{
		auto state = GetState();

		if ((state == Stream::State::ERROR) || (state == Stream::State::STOPPED))
		{
			return false;
		}

		// Check stream expired by signed policy
		if (CheckStreamExpired())
		{
			logai("Stream has expired by signed policy");
			Stop();
			return false;
		}

		if ((_remaining_data == nullptr) || _remaining_data->IsEmpty())
		{
			_remaining_data = data->Clone();
		}
		else
		{
			_remaining_data->Append(data);
		}

		logap("Trying to parse data\n%s", _remaining_data->Dump(_remaining_data->GetLength()).CStr());

		while (true)
		{
			int32_t bytes_used = _handshake_handler.IsHandshakeCompleted()
									 ? _chunk_handler.HandleData(_remaining_data)
									 : _handshake_handler.HandleData(_remaining_data);

			if (bytes_used > 0)
			{
				// Successfully parsed some data
				_remaining_data = _remaining_data->Subdata(bytes_used);
				continue;
			}

			if (bytes_used == 0)
			{
				// Need more data
				break;
			}

			logat("Could not process RTMP packet: size: %zu bytes, returns: %d",
				  _remaining_data->GetLength(),
				  bytes_used);

			Stop();
			return false;
		}

		_chunk_handler.AccumulateAcknowledgementSize(data->GetLength());

		return true;
	}

	bool RtmpStreamV2::UpdateConnectInfo(const ov::String &tc_url)
	{
		if (tc_url.IsEmpty())
		{
			return false;
		}

		// Parse the URL to obtain the domain name
		auto url = ov::Url::Parse(tc_url);

		if (url == nullptr)
		{
			logae("Invalid tcUrl: %s", tc_url.CStr());
			return false;
		}

		_name			= url->Stream();
		_tc_url			= tc_url;

		_vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(url->Host(), url->App());

		if (_vhost_app_name.IsValid())
		{
			UpdateNamePath(_vhost_app_name);
		}
		else
		{
			// Since vhost/app/stream may later be changed by AdmissionWebhooks,
			// it is set in advance even if it is not valid for now
		}

		_chunk_handler.UpdateQueueAlias();

		SetRequestedUrl(url);

		return true;
	}

	bool RtmpStreamV2::IsReadyToPublish() const
	{
		if (_chunk_handler.GetWaitingTrackCount() == 0)
		{
			// There is no track to wait
			return true;
		}

		for (auto &[key, rtmp_track] : _rtmp_track_map)
		{
			if (rtmp_track->HasTooManyPackets())
			{
				// If any track has too many buffers, force publish
				return true;
			}
		}

		// We can still wait for more tracks to be ready
		return false;
	}

	void RtmpStreamV2::SetRequestedUrlWithPort(std::shared_ptr<ov::Url> requested_url)
	{
		// PORT can be omitted (1935), but SignedPolicy requires this information.
		if (requested_url->Port() == 0)
		{
			requested_url->SetPort(_remote->GetLocalAddress()->Port());
		}

		SetRequestedUrl(requested_url);
	}

	bool RtmpStreamV2::PostPublish(const modules::rtmp::AmfDocument &document)
	{
		if (_is_post_published)
		{
			return _is_post_published;
		}

		auto requested_url = GetRequestedUrl();

		if (requested_url == nullptr)
		{
			OV_ASSERT2(false);
			return false;
		}

		{
			auto url = requested_url->Clone();

			// When using the RTMP authentication, the stream name may be provided here
			{
				auto stream_name	= document.Get(3)->GetString();

				// Append a query string if it exists
				auto query_position = stream_name.IndexOf('?');
				if (query_position >= 0)
				{
					url->AppendQueryString(stream_name.Substring(query_position));
					stream_name = stream_name.Substring(0, query_position);
				}

				if (stream_name.IsEmpty() == false)
				{
					url->SetStream(stream_name);
				}
			}

			SetRequestedUrlWithPort(url);
			SetFinalUrl(url);
		}

		_is_post_published = CheckAccessControl() && ValidatePublishUrl();

		return _is_post_published;
	}

	bool RtmpStreamV2::CheckAccessControl()
	{
		auto request_url						   = GetRequestedUrl();

		// Check SignedPolicy
		auto provider							   = GetProvider();

		auto request_info						   = std::make_shared<ac::RequestInfo>(request_url, nullptr, _remote, nullptr);

		auto [signed_policy_result, signed_policy] = provider->VerifyBySignedPolicy(request_info);
		switch (signed_policy_result)
		{
			case AccessController::VerificationResult::Error:
				logaw("SignedPolicy returned error: %s", request_url->ToUrlString().CStr());
				return false;

			case AccessController::VerificationResult::Fail:
				logaw("SignedPolicy returned failure: %s", signed_policy->GetErrMessage().CStr());
				return false;

			case AccessController::VerificationResult::Off:
				break;

			case AccessController::VerificationResult::Pass:
				_stream_expired_msec = signed_policy->GetStreamExpireEpochMSec();
				break;
		}

		auto [webhooks_result, admission_webhooks] = provider->VerifyByAdmissionWebhooks(request_info);
		switch (webhooks_result)
		{
			case AccessController::VerificationResult::Error:
				logaw("AdmissionWebhooks returned error: %s", request_url->ToUrlString().CStr());
				break;

			case AccessController::VerificationResult::Fail:
				logaw("AdmissionWebhooks returned failure: %s", admission_webhooks->GetErrReason().CStr());
				break;

			case AccessController::VerificationResult::Off:
				return true;

			case AccessController::VerificationResult::Pass: {
				// Lifetime
				if (admission_webhooks->GetLifetime() != 0)
				{
					// Choice smaller value
					auto stream_expired_msec_from_webhooks = ov::Clock::NowMSec() + admission_webhooks->GetLifetime();
					if (_stream_expired_msec == 0 || stream_expired_msec_from_webhooks < _stream_expired_msec)
					{
						_stream_expired_msec = stream_expired_msec_from_webhooks;
					}
				}

				// Redirect URL
				auto new_url = admission_webhooks->GetNewURL();

				if (new_url != nullptr)
				{
					SetFinalUrl(new_url->Clone());

					_chunk_handler.UpdateQueueAlias();
				}

				return true;
			}
		}

		return false;
	}

	bool RtmpStreamV2::ValidatePublishUrl()
	{
		auto final_url = GetFinalUrl();

		if (final_url == nullptr)
		{
			logae("Publish URL is not set");
			return false;
		}

		{
			const auto scheme = final_url->Scheme().UpperCaseString();

			if (((scheme != "RTMP") && (scheme != "RTMPS")) ||
				final_url->Host().IsEmpty() ||
				final_url->App().IsEmpty() ||
				final_url->Stream().IsEmpty())
			{
				logae("Invalid publish URL: %s", final_url->ToUrlString().CStr());
				return false;
			}
		}

		// Get an information from the final URL
		auto orchestrator	 = ocst::Orchestrator::GetInstance();
		auto vhost_app_name	 = orchestrator->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());
		const auto &app_info = orchestrator->GetApplicationInfo(vhost_app_name);

		if (app_info.IsValid() == false)
		{
			logae("Could not find application: %s (%s)", vhost_app_name.CStr(), final_url->ToUrlString(true).CStr());
			return false;
		}

		_vhost_app_name = app_info.GetVHostAppName();
		UpdateNamePath(_vhost_app_name);
		SetName(final_url->Stream());

		return true;
	}

	std::shared_ptr<RtmpTrack> RtmpStreamV2::AddRtmpTrack(std::shared_ptr<RtmpTrack> rtmp_track)
	{
		if (rtmp_track == nullptr)
		{
			OV_ASSERT2(false);
		}
		else
		{
			_rtmp_track_map[rtmp_track->GetTrackId()] = rtmp_track;
		}

		return rtmp_track;
	}

	std::shared_ptr<RtmpTrack> RtmpStreamV2::GetRtmpTrack(MediaTrackId track_id) const
	{
		auto it = _rtmp_track_map.find(track_id);
		if (it != _rtmp_track_map.end())
		{
			return it->second;
		}

		return nullptr;
	}

	void RtmpStreamV2::AdjustTimestamp(uint32_t track_id, int64_t &pts, int64_t &dts)
	{
		AdjustTimestampByBase(track_id, pts, dts, std::numeric_limits<int64_t>::max());
	}

	bool RtmpStreamV2::SetTrackInfo()
	{
		for (auto &[track_id, rtmp_track] : _rtmp_track_map)
		{
			if (rtmp_track->HasSequenceHeader() == false)
			{
				rtmp_track->SetIgnored(true);
			}

			if (rtmp_track->IsIgnored())
			{
				continue;
			}

			auto media_type = rtmp_track->GetMediaType();

			if (media_type == cmn::MediaType::Audio)
			{
				auto media_track = std::make_shared<MediaTrack>();

				_chunk_handler.FillAudioMetadata(media_track);
				rtmp_track->FillMediaTrackMetadata(media_track);

				AddTrack(media_track);
				logat("Audio track has been created: %s", media_track->GetInfoString().CStr());
			}
			else if (media_type == cmn::MediaType::Video)
			{
				auto media_track = std::make_shared<MediaTrack>();

				_chunk_handler.FillVideoMetadata(media_track);
				rtmp_track->FillMediaTrackMetadata(media_track);

				AddTrack(media_track);
				logat("Video track has been created: %s", media_track->GetInfoString().CStr());
			}
		}

		// Data Track
		if (GetFirstTrackByType(cmn::MediaType::Data) == nullptr)
		{
			auto data_track = std::make_shared<MediaTrack>();

			data_track->SetId(TRACK_ID_FOR_DATA);
			data_track->SetMediaType(cmn::MediaType::Data);
			// Since elsewhere, such as in SendDataFrame(), the data track’s timebase is assumed to be 1/1000, you must not arbitrarily change the timebase
			data_track->SetTimeBase(1, 1000);
			data_track->SetOriginBitstream(cmn::BitstreamFormat::Unknown);

			AddTrack(data_track);
			logat("Data track has been created: %s", data_track->GetInfoString().CStr());
		}

		return true;
	}

	bool RtmpStreamV2::PublishStream()
	{
		// Get application config
		auto provider = GetProvider();
		if (provider == nullptr)
		{
			logae("Could not find provider");
			OV_ASSERT2(false);
			return false;
		}

		const auto application = provider->GetApplicationByName(_vhost_app_name);
		if (application == nullptr)
		{
			logae("Could not find application");
			OV_ASSERT2(false);
			return false;
		}

		const auto &rtmp_provider = application->GetConfig().GetProviders().GetRtmpProvider();

		_chunk_handler.SetEventGeneratorConfig(rtmp_provider.GetEventGenerator());

		return SetTrackInfo() && PublishChannel(_vhost_app_name);
	}

	bool RtmpStreamV2::SendData(const std::shared_ptr<const ov::Data> &data)
	{
		if (data == nullptr)
		{
			logae("Data is null");
			OV_ASSERT2(false);
			return false;
		}

		return _remote->Send(data);
	}
}  // namespace pvd::rtmp
