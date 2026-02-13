//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../queue_metrics.h"
#include "../stream_metrics.h"
#include "alert_rules_updater.h"
#include "base/ovlibrary/ovlibrary.h"
#include "config/config.h"
#include "message.h"
#include "notification_data.h"

namespace mon::alrt
{
	class Alert
	{
	public:
		~Alert();

		bool Start(const std::shared_ptr<const cfg::Server> &server_config);
		bool Stop();

		void SendStreamMessage(Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric, const std::shared_ptr<StreamMetrics> &parent_stream_metric, const std::shared_ptr<ExtraData> &extra);
		void SendStreamMessage(Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric);

	private:
		struct StreamEvent
		{
			StreamEvent(Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric, const std::shared_ptr<StreamMetrics> &parent_stream_metric, const std::shared_ptr<ExtraData> &extra)
			{
				_code = code;
				_metric = stream_metric;
				_parent_stream_metric = parent_stream_metric;
				_extra = extra;
			}

			StreamEvent(Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric, const std::shared_ptr<ExtraData> &extra = nullptr)
			{
				_code	= code;
				_metric = stream_metric;
				_extra	= extra;
			}

			Message::Code _code;
			std::shared_ptr<StreamMetrics> _metric;
			std::shared_ptr<StreamMetrics> _parent_stream_metric = nullptr;
			std::shared_ptr<ExtraData> _extra = nullptr;
		};

		void MetricWorkerThread();
		void SendNotificationThread();

		void SendStreamMessage(const std::shared_ptr<StreamEvent> &stream_event);

		bool VerifyStreamEventRule(const cfg::alrt::rule::Rules &rules, Message::Code code);

		bool VerifyQueueCongestionRules(const cfg::alrt::rule::Rules &rules, const std::shared_ptr<QueueMetrics> &queue_metric, std::vector<std::shared_ptr<Message>> &message_list);
		void VerifyIngressMetricRules(const cfg::alrt::rule::Rules &rules, const std::shared_ptr<StreamMetrics> &stream_metric, std::vector<std::shared_ptr<Message>> &message_list);
		void VerifyVideoIngressRules(const cfg::alrt::rule::Ingress &ingress, const std::shared_ptr<MediaTrack> &video_track, std::vector<std::shared_ptr<Message>> &message_list);
		void VerifyAudioIngressRules(const cfg::alrt::rule::Ingress &ingress, const std::shared_ptr<MediaTrack> &audio_track, std::vector<std::shared_ptr<Message>> &message_list);

		bool IsAlertNeeded(const ov::String &messages_key, const std::vector<std::shared_ptr<Message>> &message_list);
		void SendNotification(const NotificationData::Type &type, const std::vector<std::shared_ptr<Message>> &message_list, const ov::String &source_uri, const std::shared_ptr<StreamMetrics> &stream_metric);
		void SendNotification(const NotificationData::Type &type, const std::vector<std::shared_ptr<Message>> &message_list, const std::map<uint32_t, std::shared_ptr<QueueMetrics>> &queue_metric_list);

		void CleanupReleasedMessages(const std::vector<ov::String> &new_messages_keys);
		bool PutVerifiedMessages(const ov::String &messages_key, std::vector<std::shared_ptr<Message>> &message_list);
		bool RemoveVerifiedMessages(const ov::String &messages_key);
		std::vector<std::shared_ptr<Message>> GetVerifiedMessages(const ov::String &messages_key);

		std::shared_ptr<const cfg::Server> _server_config = nullptr;
		std::shared_ptr<AlertRulesUpdater> _rules_updater = nullptr;

		std::map<ov::String, std::vector<std::shared_ptr<Message>>> _last_verified_messages_map;

		ov::DelayQueue _timer{"MonAlert"};

		ov::Semaphore _queue_notification;

		std::shared_ptr<NotificationData> PopNotificationData();
		ov::Queue<std::shared_ptr<NotificationData>> _notification_queue;

		std::atomic<bool> _stop_thread_flag{true};
		std::thread _notification_thread;
	};
}  // namespace mon::alrt
