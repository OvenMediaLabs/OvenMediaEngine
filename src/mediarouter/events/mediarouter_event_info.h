//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/provider/application.h>
#include <stdint.h>

#include <memory>
#include <queue>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"
#include "base/modules/data_format/amf_event/amf_event.h"
#include "base/modules/data_format/amf_event/sei_event.h"

namespace mr
{
	class EventInfo
	{
	public:
		EventInfo(cmn::BitstreamFormat event_format, cmn::PacketType event_type, bool urgent, int32_t interval)
		{
			_event_format  = event_format;
			_event_type	   = event_type;
			_urgent		   = urgent;
			_interval	   = interval;
			_keyframe_only = false;

			_stop_watch.Start();
		}
		virtual ~EventInfo() = default;

		virtual bool IsTrigger()
		{
			return _stop_watch.IsElapsed(_interval);
		}

		virtual bool Update()
		{
			return _stop_watch.Update();
		}

		void SetKeyframeOnly(bool keyframe_only)
		{
			_keyframe_only = keyframe_only;
		}

		virtual ov::String GetInfoString()
		{
			return ov::String::FormatString("EventFormat:%s, EventType:%s, Urgent:%s, Interval:%d, KeyframeOnly:%s",
											GetBitstreamFormatString(_event_format),
											GetMediaPacketTypeString(_event_type),
											_urgent ? "true" : "false",
											_interval,
											_keyframe_only ? "true" : "false");
		}

		static std::shared_ptr<mr::EventInfo> Parse(const std::shared_ptr<info::Stream> &stream_info, const pugi::xml_node &event_node);
		bool ParseValues(const pugi::xml_node &values_node);

		std::shared_ptr<ov::Data> Serialize();

		int32_t _interval;
		cmn::BitstreamFormat _event_format;
		cmn::PacketType _event_type;
		bool _urgent;
		bool _keyframe_only;
		ov::StopWatch _stop_watch;

		std::shared_ptr<SEIEvent> _sei_event				  = nullptr;
		std::shared_ptr<AmfTextDataEvent> _amf_textdata_event = nullptr;
		std::shared_ptr<AmfUserDataEvent> _amf_userdata_event = nullptr;
	};
}  // namespace mr