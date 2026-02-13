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
#include "base/info/codec.h"
#include "message.h"
#include "extra_data.h"
namespace mon::alrt
{
	class NotificationData
	{
	public:
		enum class Type : uint16_t
		{
			INGRESS,
			EGRESS,
			INTERNAL_QUEUE
		};

		constexpr static const char *StringFromType(Type type)
		{
			switch (type)
			{
				OV_CASE_RETURN_ENUM_STRING(Type, INGRESS);
				OV_CASE_RETURN_ENUM_STRING(Type, EGRESS);
				OV_CASE_RETURN_ENUM_STRING(Type, INTERNAL_QUEUE);
			}

			OV_ASSERT2(false);
			return "UNKNOWN";
		}

		constexpr static Type TypeFromMessageCode(Message::Code code)
		{
			switch (code)
			{
				case Message::Code::OK:
					return Type::INGRESS;

				case Message::Code::INGRESS_STREAM_CREATED:
				case Message::Code::INGRESS_STREAM_PREPARED:
				case Message::Code::INGRESS_STREAM_DELETED:
				case Message::Code::INGRESS_STREAM_CREATION_FAILED_DUPLICATE_NAME:
				case Message::Code::INGRESS_BITRATE_LOW:
				case Message::Code::INGRESS_BITRATE_HIGH:
				case Message::Code::INGRESS_FRAMERATE_LOW:
				case Message::Code::INGRESS_FRAMERATE_HIGH:
				case Message::Code::INGRESS_WIDTH_SMALL:
				case Message::Code::INGRESS_WIDTH_LARGE:
				case Message::Code::INGRESS_HEIGHT_SMALL:
				case Message::Code::INGRESS_HEIGHT_LARGE:
				case Message::Code::INGRESS_SAMPLERATE_LOW:
				case Message::Code::INGRESS_SAMPLERATE_HIGH:
				case Message::Code::INGRESS_LONG_KEY_FRAME_INTERVAL:
				case Message::Code::INGRESS_HAS_BFRAME:
					return Type::INGRESS;

				case Message::Code::EGRESS_STREAM_CREATED:
				case Message::Code::EGRESS_STREAM_PREPARED:
				case Message::Code::EGRESS_STREAM_DELETED:
				case Message::Code::EGRESS_STREAM_CREATION_FAILED_OUTPUT_PROFILE:
				case Message::Code::EGRESS_STREAM_CREATION_FAILED_DECODER:
				case Message::Code::EGRESS_STREAM_CREATION_FAILED_ENCODER:
				case Message::Code::EGRESS_STREAM_CREATION_FAILED_FILTER:
				case Message::Code::EGRESS_TRANSCODE_FAILED_DECODING:
				case Message::Code::EGRESS_TRANSCODE_FAILED_ENCODING:
				case Message::Code::EGRESS_TRANSCODE_FAILED_FILTERING:
				case Message::Code::EGRESS_LLHLS_READY:
				case Message::Code::EGRESS_HLS_READY:
					return Type::EGRESS;

				case Message::Code::INTERNAL_QUEUE_CONGESTION:
					return Type::INTERNAL_QUEUE;
			}

			OV_ASSERT2(false);
			return Type::INTERNAL_QUEUE;
		}

		NotificationData(const Type &type, const std::vector<std::shared_ptr<Message>> &message_list, const ov::String source_uri, const std::shared_ptr<StreamMetrics> &stream_metric);
		NotificationData(const Type &type, const std::vector<std::shared_ptr<Message>> &message_list, const std::map<uint32_t, std::shared_ptr<QueueMetrics>> &queue_metric_list);
		NotificationData(const Type &type, const std::vector<std::shared_ptr<Message>> &message_list);
		ov::String ToJsonString() const;

		void SetSourceUri(const ov::String &source_uri)
		{
			_source_uri = source_uri;
		}

		void SetStreamMetric(const std::shared_ptr<StreamMetrics> &stream_metric)
		{
			_stream_metric = stream_metric;
		}

		void SetParentStreamMetric(const std::shared_ptr<StreamMetrics> &parent_stream_metric)
		{
			_parent_stream_metric = parent_stream_metric;
		}

		void SetExtra(const std::shared_ptr<ExtraData> &extra)
		{
			_extra = extra;
		}

	private:
		Type _type;
		std::vector<std::shared_ptr<Message>> _message_list;

		ov::String _source_uri;
		std::shared_ptr<StreamMetrics> _stream_metric						  = nullptr;
		std::shared_ptr<StreamMetrics> _parent_stream_metric				  = nullptr;
		std::shared_ptr<ExtraData> _extra							  = nullptr;

		std::map<uint32_t, std::shared_ptr<QueueMetrics>> _queue_metric_list;
	};
}  // namespace mon::alrt
