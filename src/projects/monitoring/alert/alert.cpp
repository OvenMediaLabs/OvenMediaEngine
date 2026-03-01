//=============================================================================
//
//	OvenMediaEngine
//
//	Created by Gilhoon Choi
//	Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "alert.h"

#include "../monitoring_private.h"
#include "monitoring/monitoring.h"
#include "notification.h"

#define LONG_KEY_FRAME_INTERVAL_SIZE 4.0
#define NOTIFICATION_MAX_RETRY_COUNT 2

namespace mon::alrt
{
	Alert::~Alert()
	{
		Stop();
	}

	bool Alert::Start(const std::shared_ptr<const cfg::Server> &server_config)
	{
		if (!_stop_thread_flag)
		{
			return true;
		}

		if (server_config == nullptr)
		{
			return false;
		}

		auto alert = server_config->GetAlert();

		if (alert.IsParsed() == false)
		{
			// Doesn't use the Alert feature.
			return false;
		}

		auto notification_server_url = ov::Url::Parse(alert.GetUrl());
		if (notification_server_url == nullptr)
		{
			logte("Could not parse notification url: %s", alert.GetUrl().CStr());
			return false;
		}

		_server_config = server_config;

		_rules_updater = std::make_shared<AlertRulesUpdater>(alert);
		_rules_updater->UpdateIfNeeded();

		_stop_thread_flag	 = false;
		_notification_thread = std::thread(&Alert::SendNotificationThread, this);

		pthread_setname_np(_notification_thread.native_handle(), "AL");

		_timer.Push(
			[this](void *paramter) -> ov::DelayQueueAction {
				MetricWorkerThread();
				return ov::DelayQueueAction::Repeat;
			},
			500);
		_timer.Start();

		return true;
	}

	bool Alert::Stop()
	{
		if (_stop_thread_flag)
		{
			return true;
		}

		_stop_thread_flag = true;

		_timer.Stop();

		_notification_queue.Stop();
		_queue_notification.Stop();

		if (_notification_thread.joinable())
		{
			_notification_thread.join();
		}

		return true;
	}

	void Alert::MetricWorkerThread()
	{
		auto rules = _rules_updater->GetRules();

		NotificationData::Type type;
		ov::String messages_key;
		std::vector<std::shared_ptr<Message>> message_list;
		std::vector<ov::String> new_messages_keys;

		{
			// Check Internal queues

			type		 = NotificationData::Type::INTERNAL_QUEUE;

			messages_key = NotificationData::StringFromType(type);
			new_messages_keys.push_back(messages_key);

			std::map<uint32_t, std::shared_ptr<QueueMetrics>> congest_queue_metric_list;
			const auto queue_metric_list = MonitorInstance->GetServerMetrics()->GetQueueMetricsList();
			for (const auto &[queue_key, queue_metric] : queue_metric_list)
			{
				if (!VerifyQueueCongestionRules(*rules, queue_metric, message_list))
				{
					congest_queue_metric_list[queue_key] = queue_metric;
				}
			}

			if (IsAlertNeeded(messages_key, message_list))
			{
				SendNotification(type, message_list, congest_queue_metric_list);
			}

			PutVerifiedMessages(messages_key, message_list);
		}

		{
			// Check streams

			for (const auto &[host_key, host_metric] : MonitorInstance->GetHostMetricsList())
			{
				for (const auto &[app_key, app_metric] : host_metric->GetApplicationMetricsList())
				{
					for (const auto &[stream_key, stream_metric] : app_metric->GetStreamMetricsMap())
					{
						message_list.clear();

						if (stream_metric->IsInputStream())
						{
							type		 = NotificationData::Type::INGRESS;

							messages_key = stream_metric->GetUri();
							new_messages_keys.push_back(messages_key);

							VerifyIngressMetricRules(*rules, stream_metric, message_list);

							if (IsAlertNeeded(messages_key, message_list))
							{
								SendNotification(type, message_list, stream_metric->GetUri(), stream_metric);
							}

							PutVerifiedMessages(messages_key, message_list);
						}
					}
				}
			}
		}

		CleanupReleasedMessages(new_messages_keys);

		_rules_updater->UpdateIfNeeded();
	}

	void Alert::SendNotificationThread()
	{
		ov::logger::ThreadHelper thread_helper;

		while (!_stop_thread_flag)
		{
			_queue_notification.Wait();

			auto notification_data = PopNotificationData();
			if (notification_data == nullptr)
			{
				continue;
			}

			auto alert		  = _server_config->GetAlert();

			auto message_body = notification_data->ToJsonString();
			if (message_body.IsEmpty())
			{
				logte("Message body is empty");
				continue;
			}

			int retry_count = 0;

			while (true)
			{
				// Notification
				auto notification_server_url = ov::Url::Parse(alert.GetUrl());
				std::shared_ptr<Notification> notification_response = Notification::Query(notification_server_url, alert.GetTimeoutMsec(), alert.GetSecretKey(), message_body);
				if (notification_response == nullptr)
				{
					// Probably this doesn't happen
					logte("Could not load Notification");
					break;
				}

				if (notification_response->GetStatusCode() == Notification::StatusCode::INTERNAL_ERROR)
				{
					retry_count++;

					if (NOTIFICATION_MAX_RETRY_COUNT < retry_count)
					{
						break;
					}
					else
					{
						logte("Notification internal error occurred. Retrying... [%d / %d]", retry_count, NOTIFICATION_MAX_RETRY_COUNT);
						continue;
					}
				}

				break;
			}
		}
	}

	template <typename T>
	void AddNonOkMessage(std::vector<std::shared_ptr<Message>> &message_list, Message::Code code, T config_value, T measured_value)
	{
		if (code != Message::Code::OK)
		{
			ov::String description = Message::DescriptionFromMessageCode(code, config_value, measured_value);
			auto message		   = Message::CreateMessage(code, description);

			message_list.push_back(message);
		}
	}

	void Alert::SendStreamMessage(Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric, const std::shared_ptr<StreamMetrics> &parent_stream_metric, const std::shared_ptr<ExtraData> &extra)
	{
		if (_stop_thread_flag)
		{
			return;
		}

		auto rules = _rules_updater->GetRules();

		if (!VerifyStreamEventRule(*rules, code))
		{
			return;
		}

		auto stream_event = std::make_shared<StreamEvent>(code, stream_metric, parent_stream_metric, extra);
		SendStreamMessage(stream_event);
	}

	void Alert::SendStreamMessage(Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric)
	{
		if (_stop_thread_flag)
		{
			return;
		}

		auto rules = _rules_updater->GetRules();

		if (!VerifyStreamEventRule(*rules, code))
		{
			return;
		}

		auto stream_event = std::make_shared<StreamEvent>(code, stream_metric);
		SendStreamMessage(stream_event);
	}

	void Alert::SendStreamMessage(const std::shared_ptr<StreamEvent> &stream_event)
	{
		if (stream_event == nullptr)
		{
			return;
		}

		auto code = stream_event->_code;
		auto stream_metric = stream_event->_metric;
		auto parent_stream_metric = stream_event->_parent_stream_metric;
		auto extra = stream_event->_extra;

		ov::String description = Message::DescriptionFromMessageCode(code);
		auto message = Message::CreateMessage(code, description);

		std::vector<std::shared_ptr<Message>> message_list(1, message);

		ov::String messages_key;
		if (stream_metric)
		{
			messages_key = stream_metric->GetUri();
		}
		else if (parent_stream_metric)
		{
			messages_key = parent_stream_metric->GetUri();
		}
		else
		{
			logtw("Invalid stream event with null stream metric and parent source info. code: %s", Message::StringFromMessageCode(code));
			return;
		}

		auto type = NotificationData::TypeFromMessageCode(code);

		if (IsAlertNeeded(messages_key, message_list))
		{
			auto data = std::make_shared<NotificationData>(type, message_list);

			if (stream_metric != nullptr)
			{
				data->SetStreamMetric(stream_metric);
				data->SetSourceUri(stream_metric->GetUri());
			}

			if (parent_stream_metric != nullptr)
			{
				data->SetParentStreamMetric(parent_stream_metric);
			}

			if(extra != nullptr)
			{
				data->SetExtra(extra);
			}

			_notification_queue.Enqueue(data);
			_queue_notification.Notify();
		}
	}

	bool Alert::VerifyStreamEventRule(const cfg::alrt::rule::Rules &rules, Message::Code code)
	{
		auto raw_code = ov::ToUnderlyingType(code);

		if (OV_CHECK_FLAG(raw_code, Message::INGRESS_CODE_STATUS_MASK))
		{
			auto ingress = rules.GetIngress();

			return ingress.IsParsed() && ingress.IsStreamStatus();
		}
		else if (OV_CHECK_FLAG(raw_code, Message::EGRESS_CODE_STATUS_MASK))
		{
			auto egress = rules.GetEgress();

			return egress.IsParsed() && egress.IsStreamStatus();
		}
		else if (OV_CHECK_FLAG(raw_code, Message::EGRESS_CODE_READY_MASK))
		{
			auto egress = rules.GetEgress();

			if (egress.IsParsed() == false)
			{
				return false;
			}

			if ((code == Message::Code::EGRESS_LLHLS_READY) && (egress.IsLLHLSReady() == false))
			{
				return false;
			}
			else if ((code == Message::Code::EGRESS_HLS_READY) && (egress.IsHLSReady() == false))
			{
				return false;
			}

			return true;
		}
		else if(OV_CHECK_FLAG(raw_code, Message::EGRESS_CODE_TRANSCODE_MASK))
		{
			auto egress = rules.GetEgress();

			return egress.IsParsed() && egress.IsTranscodeStatus();
		}

		// Invalid message code for stream
		logtw("Invalid message code: %s", Message::StringFromMessageCode(code));
		return false;
	}

	bool Alert::VerifyQueueCongestionRules(const cfg::alrt::rule::Rules &rules, const std::shared_ptr<QueueMetrics> &queue_metric, std::vector<std::shared_ptr<Message>> &message_list)
	{
		if (!rules.IsInternalQueueCongestion())
		{
			return true;
		}

		if ((queue_metric->GetThreshold()) > 0 && (queue_metric->GetSize() > queue_metric->GetThreshold()))
		{
			AddNonOkMessage<size_t>(message_list, Message::Code::INTERNAL_QUEUE_CONGESTION, queue_metric->GetThreshold(), queue_metric->GetSize());

			return false;
		}

		return true;
	}

	void Alert::VerifyIngressMetricRules(const cfg::alrt::rule::Rules &rules, const std::shared_ptr<StreamMetrics> &stream_metric, std::vector<std::shared_ptr<Message>> &message_list)
	{
		auto ingress = rules.GetIngress();
		if (!ingress.IsParsed())
		{
			return;
		}

		int32_t totalBitrate = 0;

		for (auto &[track_id, track] : stream_metric->GetTracks())
		{
			if (track->GetMediaType() == cmn::MediaType::Video)
			{
				totalBitrate += track->GetBitrateByMeasured();
				VerifyVideoIngressRules(ingress, track, message_list);
			}
			else if (track->GetMediaType() == cmn::MediaType::Audio)
			{
				totalBitrate += track->GetBitrateByMeasured();
				VerifyAudioIngressRules(ingress, track, message_list);
			}
		}

		if (totalBitrate > 0)
		{
			// Verify MinBitrates
			if (ingress.GetMinBitrate() > 0)
			{
				if (totalBitrate < ingress.GetMinBitrate())
				{
					AddNonOkMessage<int32_t>(message_list, Message::Code::INGRESS_BITRATE_LOW, ingress.GetMinBitrate(), totalBitrate);
				}
			}

			// Verify MaxBitrates
			if (ingress.GetMaxBitrate() > 0)
			{
				if (totalBitrate > ingress.GetMaxBitrate())
				{
					AddNonOkMessage<int32_t>(message_list, Message::Code::INGRESS_BITRATE_HIGH, ingress.GetMaxBitrate(), totalBitrate);
				}
			}
		}
	}

	void Alert::VerifyVideoIngressRules(const cfg::alrt::rule::Ingress &ingress, const std::shared_ptr<MediaTrack> &video_track, std::vector<std::shared_ptr<Message>> &message_list)
	{
		// Verify HasBFrame
		if (ingress.GetHasBFrames())
		{
			if (video_track->HasBframes())
			{
				AddNonOkMessage<bool>(message_list, Message::Code::INGRESS_HAS_BFRAME, true, true);
			}
		}

		if (video_track->GetFrameRateByMeasured() > 0)
		{
			// Verify MinFramerate
			if (ingress.GetMinFramerate() > 0)
			{
				if (video_track->GetFrameRateByMeasured() < ingress.GetMinFramerate())
				{
					AddNonOkMessage<double>(message_list, Message::Code::INGRESS_FRAMERATE_LOW, ingress.GetMinFramerate(), video_track->GetFrameRateByMeasured());
				}
			}

			// Verify MaxFramerate
			if (ingress.GetMaxFramerate() > 0)
			{
				if (video_track->GetFrameRateByMeasured() > ingress.GetMaxFramerate())
				{
					AddNonOkMessage<double>(message_list, Message::Code::INGRESS_FRAMERATE_HIGH, ingress.GetMaxFramerate(), video_track->GetFrameRateByMeasured());
				}
			}

			// Verify LongKeyFrameInterval
			if (video_track->GetKeyFrameInterval() > 0 && ingress.IsLongKeyFrameInterval())
			{
				double interval = video_track->GetKeyFrameInterval() / video_track->GetFrameRateByMeasured();
				if (interval > LONG_KEY_FRAME_INTERVAL_SIZE)
				{
					AddNonOkMessage<double>(message_list, Message::Code::INGRESS_LONG_KEY_FRAME_INTERVAL, LONG_KEY_FRAME_INTERVAL_SIZE, interval);
				}
			}
		}

		auto video_resolution = video_track->GetResolution();
		if (video_resolution.width > 0)
		{
			// Verify MinWidth
			if (ingress.GetMinWidth() > 0)
			{
				if (video_resolution.width < ingress.GetMinWidth())
				{
					AddNonOkMessage<int32_t>(message_list, Message::Code::INGRESS_WIDTH_SMALL, ingress.GetMinWidth(), video_resolution.width);
				}
			}

			// Verify MaxWidth
			if (ingress.GetMaxWidth() > 0)
			{
				if (video_resolution.width > ingress.GetMaxWidth())
				{
					AddNonOkMessage<int32_t>(message_list, Message::Code::INGRESS_WIDTH_LARGE, ingress.GetMaxWidth(), video_resolution.width);
				}
			}
		}

		if (video_resolution.height > 0)
		{
			// Verify MinHeight
			if (ingress.GetMinHeight() > 0)
			{
				if (video_resolution.height < ingress.GetMinHeight())
				{
					AddNonOkMessage<int32_t>(message_list, Message::Code::INGRESS_HEIGHT_SMALL, ingress.GetMinHeight(), video_resolution.height);
				}
			}

			// Verify MaxHeight
			if (ingress.GetMaxHeight() > 0)
			{
				if (video_resolution.height > ingress.GetMaxHeight())
				{
					AddNonOkMessage<int32_t>(message_list, Message::Code::INGRESS_HEIGHT_LARGE, ingress.GetMaxHeight(), video_resolution.height);
				}
			}
		}
	}

	void Alert::VerifyAudioIngressRules(const cfg::alrt::rule::Ingress &ingress, const std::shared_ptr<MediaTrack> &audio_track, std::vector<std::shared_ptr<Message>> &message_list)
	{
		if (audio_track->GetSampleRate() > 0)
		{
			// Verify MinSamplerate
			if (ingress.GetMinSamplerate() > 0)
			{
				if (audio_track->GetSampleRate() < ingress.GetMinSamplerate())
				{
					AddNonOkMessage<int32_t>(message_list, Message::Code::INGRESS_SAMPLERATE_LOW, ingress.GetMinSamplerate(), audio_track->GetSampleRate());
				}
			}

			// Verify MaxSamplerate
			if (ingress.GetMaxSamplerate() > 0)
			{
				if (audio_track->GetSampleRate() > ingress.GetMaxSamplerate())
				{
					AddNonOkMessage<int32_t>(message_list, Message::Code::INGRESS_SAMPLERATE_HIGH, ingress.GetMinSamplerate(), audio_track->GetSampleRate());
				}
			}
		}
	}

	bool Alert::IsAlertNeeded(const ov::String &messages_key, const std::vector<std::shared_ptr<Message>> &message_list)
	{
		auto last_verified_message_list = GetVerifiedMessages(messages_key);

		if (last_verified_message_list.size() == 0)
		{
			// New messages

			if (message_list.size() > 0)
			{
				return true;
			}
		}
		else
		{
			// Changed messages

			// Compare the previously sent Alert Messages with the new Alert Messages to check if there are any changes.
			if (last_verified_message_list.size() != message_list.size())
			{
				return true;
			}
			else
			{
				for (size_t i = 0; i < last_verified_message_list.size(); ++i)
				{
					auto alerted_message = last_verified_message_list[i];
					auto new_message	 = message_list[i];

					if (alerted_message->GetCode() != new_message->GetCode())
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	void Alert::SendNotification(const NotificationData::Type &type, const std::vector<std::shared_ptr<Message>> &message_list, const ov::String &source_uri, const std::shared_ptr<StreamMetrics> &stream_metric)
	{
		_notification_queue.Enqueue(std::make_shared<NotificationData>(type, message_list, source_uri, stream_metric));
		_queue_notification.Notify();
	}

	void Alert::SendNotification(const NotificationData::Type &type, const std::vector<std::shared_ptr<Message>> &message_list, const std::map<uint32_t, std::shared_ptr<QueueMetrics>> &queue_metric_list)
	{
		_notification_queue.Enqueue(std::make_shared<NotificationData>(type, message_list, queue_metric_list));
		_queue_notification.Notify();
	}

	void Alert::CleanupReleasedMessages(const std::vector<ov::String> &new_messages_keys)
	{
		// Find and cleanup the messages that have already been released among the alerts that were sent.

		std::vector<ov::String> messages_keys_to_cleanup;
		for (const auto &[messages_key, verified_messages] : _last_verified_messages_map)
		{
			bool exist = false;
			for (const auto &new_messages_key : new_messages_keys)
			{
				if (messages_key == new_messages_key)
				{
					exist = true;
					break;
				}
			}

			if (!exist)
			{
				messages_keys_to_cleanup.push_back(messages_key);
			}
		}

		for (auto const &messages_key : messages_keys_to_cleanup)
		{
			RemoveVerifiedMessages(messages_key);
		}
	}

	bool Alert::PutVerifiedMessages(const ov::String &messages_key, std::vector<std::shared_ptr<Message>> &message_list)
	{
		if (messages_key.IsEmpty())
		{
			return false;
		}

		RemoveVerifiedMessages(messages_key);

		_last_verified_messages_map.emplace(messages_key, std::move(message_list));

		return true;
	}

	bool Alert::RemoveVerifiedMessages(const ov::String &messages_key)
	{
		if (messages_key.IsEmpty())
		{
			return false;
		}

		auto messages = _last_verified_messages_map.find(messages_key);
		if (messages != _last_verified_messages_map.end())
		{
			_last_verified_messages_map.erase(messages);
		}

		return true;
	}

	std::vector<std::shared_ptr<Message>> Alert::GetVerifiedMessages(const ov::String &messages_key)
	{
		if (messages_key.IsEmpty())
		{
			return {};
		}

		auto item = _last_verified_messages_map.find(messages_key);
		if (item == _last_verified_messages_map.end())
		{
			return {};
		}

		return item->second;
	}

	std::shared_ptr<NotificationData> Alert::PopNotificationData()
	{
		if (_notification_queue.IsEmpty())
		{
			return nullptr;
		}

		auto notification = _notification_queue.Dequeue();
		if (notification.has_value())
		{
			return notification.value();
		}

		return nullptr;
	}
}  // namespace mon::alrt
