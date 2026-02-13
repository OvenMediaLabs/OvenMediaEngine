//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/vhost_app_name.h>

#include <shared_mutex>

#include "base/ovlibrary/ovlibrary.h"

namespace info
{
	typedef uint32_t managed_queue_id_t;

	class ManagedQueue
	{
		// Create URN specification for queue
		/*
			[URN Pattern]
				- mngq:v={VhostName}#{AppName}[s=/{StreamName}]:p={PART}:n={NAME}

			[PART]
				- pvd: Provider
				- imr: Inbound Mediarouter
				- trs: Transcoder
				- omr: Outbound Mediarouter
				- pub: Publisher
				- och: Ochestrator

			[NAME]
				- filter_{video|audio}
				- encoder_{codec_name}_{trackid}
				- decoder_{codec_name}_{trackid}
				- appworker_[{protocol}]_{id}
				- stremworker_[{protocol}]_{id}

			examples
				- mngq:v=#default#app:s=stream:p=trs:n=decoer_h264_0
				- mngq:v=#default#app:s=stream:p=trs:n=filter_video
				- mngq:v=#default#app:s=stream:p=trs:n=filter_audio
				- mngq:v=#default#app:s=stream:p=trs:n=encoder_opus_0
				- mngq:v=#default#app:s=stream:p=trs:n=encoder_h264_1
				- mngq:v=#default#app:p=imr:n=indicator
				- mngq:v=#default#app:p=omr:n=appworker
				- mngq:v=#default#app:p=pub:n=appworker
		*/
	public:
		class URN
		{
		public:
			URN(info::VHostAppName vhost_app_name, ov::String stream_name = nullptr, ov::String part = nullptr, ov::String name = nullptr)
				: _vhost_app_name(vhost_app_name),
				  _stream_name(stream_name),
				  _part(part),
				  _name(name)
			{
			}

			URN(ov::String vhost_app_name, ov::String stream_name = nullptr, ov::String part = nullptr, ov::String name = nullptr)
				: _vhost_app_name(vhost_app_name),
				  _stream_name(stream_name),
				  _part(part),
				  _name(name)
			{
			}

			info::VHostAppName& GetVHostAppName()
			{
				return _vhost_app_name;
			}
			ov::String& GetStreamName()
			{
				return _stream_name;
			}
			ov::String& GetPart()
			{
				return _part;
			}
			ov::String& GetName()
			{
				return _name;
			}

			ov::String ToString()
			{
				ov::String str = "mngq";

				if (_vhost_app_name.IsValid())
				{
					str.Append(":");
					str.Append("v=");
					str.Append(_vhost_app_name.ToString());
				}
				if (!_stream_name.IsEmpty())
				{
					str.Append(":");
					str.Append("s=");
					str.Append(_stream_name);
				}

				if (!_part.IsEmpty())
				{
					str.Append(":");
					str.Append("p=");
					str.Append(_part);
				}

				if (!_name.IsEmpty())
				{
					str.Append(":");
					str.Append("n=");
					str.Append(_name);
				}

				return str;
			}

		protected:
			info::VHostAppName _vhost_app_name;
			ov::String _stream_name;
			ov::String _part;
			ov::String _name;
		};

	public:
		explicit ManagedQueue(size_t threshold = 0)
			: _peak(0),
			  _size(0),
			  _threshold(threshold),
			  _threshold_exceeded_time_in_us(0),
			  _input_message_count(0),
			  _output_message_count(0),
			  _input_message_per_second(0),
			  _output_message_per_second(0),
			  _waiting_time_in_us(0),
			  _drop_message_count(0){};

		void SetId(info::managed_queue_id_t id)
		{
			_id = id;
		}

		info::managed_queue_id_t GetId() const
		{
			return _id;
		}

		ov::String GetTypeName() const
		{
			return _type_name;
		}

		void SetThreshold(size_t threshold)
		{
			auto lock_guard = std::lock_guard(_name_mutex);

			_threshold = threshold;
		}

		size_t GetThreshold() const
		{
			return _threshold;
		}

		bool IsThresholdExceeded() const
		{
			return _size > _threshold;
		}

		size_t GetPeak() const
		{
			return _peak;
		}

		size_t GetSize() const
		{
			return _size;
		}

		size_t GetInputMessagePerSecond() const
		{
			return _input_message_per_second;
		}

		size_t GetOutputMessagePerSecond() const
		{
			return _output_message_per_second;
		}

		uint64_t GetDropCount() const
		{
			return _drop_message_count;
		}

		int64_t GetWaitingTimeInUs() const
		{
			return _waiting_time_in_us;
		}

		int64_t GetThresholdExceededTimeInUs() const
		{
			return _threshold_exceeded_time_in_us;
		}

		void SetUrn(std::shared_ptr<URN> urn, const char* type_name)
		{
			auto lock_guard = std::lock_guard(_name_mutex);

			_type_name = type_name;
			_urn = urn;
		}

		std::shared_ptr<URN> GetUrn() const
		{
			return _urn;
		}

		ov::String ToString() const
		{
			if(_urn == nullptr)
			{
				return "No Urn";
			}

			return _urn->ToString();
		}

		info::managed_queue_id_t IssueUniqueQueueId()
		{
			static std::atomic<info::managed_queue_id_t> last_issued_queue_id(100);

			return last_issued_queue_id++;
		}

	protected:
		// ID of the queue
		managed_queue_id_t _id = 0;

		// Name of the queue
		std::shared_mutex _name_mutex;
		std::shared_ptr<URN> _urn;

		// Type of template
		ov::String _type_name;

		// Peak size of the queue
		size_t _peak = 0;

		// Current size of the queue
		size_t _size = 0;

		// Threshold of the queue
		size_t _threshold = 0;

		// threshold_exceeded_time increases from the point the queue is exceeded
		int64_t _threshold_exceeded_time_in_us;

		// Input Message Count
		int64_t _input_message_count = 0;
		int64_t _last_input_message_count = 0;
		// Output Message Count
		int64_t _output_message_count = 0;
		int64_t _last_output_message_count = 0;

		// Input Message Per Second
		size_t _input_message_per_second = 0;

		// Output Message Per Second
		size_t _output_message_per_second = 0;

		// Average Waiting Time(microseconds)
		int64_t _waiting_time_in_us = 0;

		// Drop Count
		uint64_t _drop_message_count = 0;
	};

}  // namespace info