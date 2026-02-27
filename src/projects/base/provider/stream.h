//==============================================================================
//
//  Provider Base Class 
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <set>
#include <base/common_types.h>
#include <base/info/stream.h>
#include <base/ovlibrary/lip_sync_clock.h>
#include "monitoring/monitoring.h"

#include <base/mediarouter/media_buffer.h>
#include <base/event/media_event.h>
#include <base/mediarouter/mediarouter_interface.h>

namespace pvd
{
	class Application;

	class Stream : public info::Stream, public ov::EnableSharedFromThis<Stream>
	{
	public:
		enum class State
		{
			IDLE,
			CONNECTED,
			DESCRIBED,
			PLAYING,
			STOPPED,	// will be retried, Set super class
			ERROR,		// will be retried
			TERMINATED	// will be deleted, Set super class
		};

		enum class DirectionType : uint8_t
		{
			UNSPECIFIED,
			PULL,
			PUSH
		};

		State GetState() const {return _state;};

		void SetApplication(const std::shared_ptr<pvd::Application> &application)
		{
			_application = application;
		}

		const char* GetApplicationTypeName();

		const std::shared_ptr<pvd::Application> &GetApplication()
		{
			return _application;
		}

		std::shared_ptr<const pvd::Application> GetApplication() const
		{
			return _application;
		}

		virtual bool Start();
		virtual bool Stop();
		virtual bool Terminate();

		// Given that the data track’s timebase is 1/1000, timestamps are treated in milliseconds
		bool SendDataFrame(int64_t timestamp_in_ms, const cmn::BitstreamFormat &format, const cmn::PacketType &packet_type, const std::shared_ptr<ov::Data> &frame, bool urgent, bool internal = false, const MediaPacketFlag packet_flag = MediaPacketFlag::NoFlag);
		bool SendDataFrame(int64_t timestamp, int64_t duration, const cmn::BitstreamFormat &format, const cmn::PacketType &packet_type, const std::shared_ptr<ov::Data> &frame, bool urgent, bool internal, const MediaPacketFlag packet_flag);

		bool SendSubtitleFrame(const ov::String &label, int64_t timestamp_in_ms, int64_t duration_ms, const cmn::BitstreamFormat &format, const std::shared_ptr<ov::Data> &frame, bool urgent);

		// Provider can override this function to handle the event if needed.
		virtual bool SendEvent(const std::shared_ptr<MediaEvent> &event);

		std::shared_ptr<const ov::Url> GetRequestedUrl() const;
		void SetRequestedUrl(const std::shared_ptr<ov::Url> &requested_url);

		std::shared_ptr<const ov::Url> GetFinalUrl() const;
		void SetFinalUrl(const std::shared_ptr<ov::Url> &final_url);

		int64_t GetCurrentTimestampMs();

	protected:
		Stream(const std::shared_ptr<pvd::Application> &application, StreamSourceType source_type);
		Stream(const std::shared_ptr<pvd::Application> &application, info::stream_id_t stream_id, StreamSourceType source_type);
		Stream(const std::shared_ptr<pvd::Application> &application, const info::Stream &stream_info);
		Stream(StreamSourceType source_type);

		virtual ~Stream();

		virtual DirectionType GetDirectionType()
		{
			return DirectionType::UNSPECIFIED;
		}

		bool UpdateStream();

		bool SetState(State state);
		bool SendFrame(const std::shared_ptr<MediaPacket> &packet);

		int64_t AdjustTimestampByBase(uint32_t track_id, int64_t &pts, int64_t &dts, int64_t max_timestamp, int64_t duration = 0);

		// For RTP
		void RegisterRtpClock(uint32_t track_id, double clock_rate);
		void UpdateSenderReportTimestamp(uint32_t track_id, uint32_t msw, uint32_t lsw, uint32_t timestamp);
		bool AdjustRtpTimestamp(uint32_t track_id, int64_t timestamp, int64_t max_timestamp, int64_t &adjusted_timestamp);
		int64_t AdjustTimestampByDelta(uint32_t track_id, int64_t timestamp, int64_t max_timestamp);

		int64_t GetBaseTimestamp(uint32_t track_id);
		
	protected:
		inline int64_t Rescale(int64_t value, int64_t to_timescale, int64_t from_timescale) 
		{
			return ((value / from_timescale) * to_timescale) + (((value % from_timescale) * to_timescale + (from_timescale / 2)) / from_timescale);
		}

	private:
		void ResetSourceStreamTimestamp();
		int64_t GetDeltaTimestamp(uint32_t track_id, int64_t timestamp, int64_t max_timestamp);
		void UpdateReconnectTimeToBasetime();

		// Processing events
		bool ProcessEvent(const std::shared_ptr<MediaEvent> &event);

		// TrackID : Timestamp(us)
		// For the by delta update method
		std::map<uint32_t, int64_t>			_source_timestamp_map;

		// For the by base timestamp method
		std::map<uint32_t, int64_t>			_last_timestamp_us_map;
		std::map<uint32_t, int64_t>			_last_duration_us_map;

		int64_t 							_base_timestamp_us = -1;

		// For Wraparound
		std::map<uint32_t, int64_t>			_last_origin_ts_map[2];
		std::map<uint32_t, int64_t>			_wraparound_count_map[2]; // 0 : pts 1: dts

		int64_t								_start_timestamp_us = -1LL; // Make first timestamp to zero

		std::chrono::time_point<std::chrono::system_clock>	_last_pkt_received_time = std::chrono::time_point<std::chrono::system_clock>::min();

		State 	_state = State::IDLE;

		std::shared_ptr<ov::Url> _requested_url = nullptr;
		std::shared_ptr<ov::Url> _final_url = nullptr;

		// Special timestamp calculation for RTP
		enum class RtpTimestampCalculationMethod : uint8_t
		{
			UNDER_DECISION,
			SINGLE_DELTA,
			WITH_RTCP_SR
		};

		RtpTimestampCalculationMethod _rtp_timestamp_method = RtpTimestampCalculationMethod::UNDER_DECISION;

		LipSyncClock 						_rtp_lip_sync_clock;
		ov::StopWatch						_first_rtp_received_time;

		// Per-track fallback to SINGLE_DELTA when SR is never received for a specific track
		std::set<uint32_t>					_sr_fallback_tracks;
		std::map<uint32_t, ov::StopWatch>	_per_track_sr_wait;

		int64_t _last_media_timestamp_ms = -1LL;
		ov::StopWatch _elapsed_from_last_media_timestamp;
		int64_t _max_generated_timestamp_ms = -1LL;

		std::shared_ptr<pvd::Application> _application = nullptr;
	};
}