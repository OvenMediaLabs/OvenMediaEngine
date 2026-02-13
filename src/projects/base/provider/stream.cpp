//==============================================================================
//
//  Provider Base Class
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "stream.h"

#include "application.h"
#include "base/info/application.h"
#include "provider_private.h"
#include "base/provider/pull_provider/stream_props.h"
#include "base/provider/pull_provider/stream.h"
#include <base/event/command/commands.h>

namespace pvd
{
	Stream::Stream(StreamSourceType source_type)
		: info::Stream(source_type),
		  _application(nullptr)
	{
	}

	Stream::Stream(const std::shared_ptr<pvd::Application> &application, StreamSourceType source_type)
		: info::Stream(*(std::static_pointer_cast<info::Application>(application)), source_type),
		  _application(application)
	{
	}

	Stream::Stream(const std::shared_ptr<pvd::Application> &application, info::stream_id_t stream_id, StreamSourceType source_type)
		: info::Stream((*std::static_pointer_cast<info::Application>(application)), stream_id, source_type),
		  _application(application)
	{
	}

	Stream::Stream(const std::shared_ptr<pvd::Application> &application, const info::Stream &stream_info)
		: info::Stream(stream_info),
		  _application(application)
	{
	}

	Stream::~Stream()
	{
	}

	bool Stream::Start()
	{
		logti("%s/%s(%u) has been started stream", GetApplicationName(), GetName().CStr(), GetId());

		UpdateReconnectTimeToBasetime();

		return true;
	}

	bool Stream::Stop()
	{
		if (GetState() == Stream::State::STOPPED)
		{
			return true;
		}

		logti("%s/%s(%u) has been stopped playing stream", GetApplicationName(), GetName().CStr(), GetId());
		ResetSourceStreamTimestamp();

		_state = State::STOPPED;

		return true;
	}

	bool Stream::Terminate()
	{
		_state = State::TERMINATED;
		return true;
	}

	// Consider the reconnection time and add it to the base timestamp
	void Stream::UpdateReconnectTimeToBasetime()
	{
		if (_last_pkt_received_time != std::chrono::time_point<std::chrono::system_clock>::min())
		{
			auto reconnection_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - _last_pkt_received_time).count();

			logti("Time taken to reconnect is %" PRId64 " milliseconds. add to the basetime", reconnection_time_us/1000);

			_base_timestamp_us += reconnection_time_us;
		}
	}

	const char *Stream::GetApplicationTypeName()
	{
		if (GetApplication() == nullptr)
		{
			return "Unknown";
		}

		return GetApplication()->GetApplicationTypeName();
	}

	int64_t Stream::GetCurrentTimestampMs()
	{
		// Not yet started
		if (_last_media_timestamp_ms == -1)
		{
			_max_generated_timestamp_ms = -1;
			return -1;
		}

		int64_t current_estimated_ts = _last_media_timestamp_ms + _elapsed_from_last_media_timestamp.Elapsed();
		if (current_estimated_ts <= _max_generated_timestamp_ms)
		{
			current_estimated_ts = _max_generated_timestamp_ms + 1;
			logti("%s/%s(%u) Adjust current estimated timestamp to avoid going backward - last_media_timestamp_ms: %" PRId64 ", elapsed_from_last_media_timestamp: %" PRId64 ", adjusted_timestamp: %" PRId64 " ms",
			GetApplicationName(), GetName().CStr(), GetId(),
			_last_media_timestamp_ms, _elapsed_from_last_media_timestamp.Elapsed(), current_estimated_ts);
		}

		_max_generated_timestamp_ms = current_estimated_ts;

		return _max_generated_timestamp_ms;
	}

	bool Stream::SendDataFrame(int64_t timestamp_in_ms, int64_t duration, const cmn::BitstreamFormat &format, const cmn::PacketType &packet_type, const std::shared_ptr<ov::Data> &frame, bool urgent, bool internal, const MediaPacketFlag packet_flag)
	{
		if (frame == nullptr)
		{
			return false;
		}

		auto data_track = GetFirstTrackByType(cmn::MediaType::Data);
		if (data_track == nullptr)
		{
			logte("Data track is not found. %s/%s(%u)", GetApplicationName(), GetName().CStr(), GetId());
			return false;
		}

		if (timestamp_in_ms == -1)
		{
			timestamp_in_ms = GetCurrentTimestampMs();
			if (timestamp_in_ms == -1)
			{
				logte("Could not send data frame. %s/%s(%u) - Media is not started yet", GetApplicationName(), GetName().CStr(), GetId());
				return false;
			}

			logtt("SendDataFrame - %s/%s(%u) - last_media_timestamp_ms: %lld, elapsed_from_last_media_timestamp: %lld, timestamp: %lld ms",
				  GetApplicationName(), GetName().CStr(), GetId(),
				  _last_media_timestamp_ms, _elapsed_from_last_media_timestamp.Elapsed(), timestamp_in_ms);
		}

		auto timestamp_in_tb = static_cast<int64_t>(timestamp_in_ms * data_track->GetTimeBase().GetTimescale() / 1000.0);

		if (format == cmn::BitstreamFormat::SCTE35 || format == cmn::BitstreamFormat::CUE)
		{
			// SCTE35 and CUE should be in the same sequence number in HLS. However, A and V can differ by up to the maximum keyframe interval duration, so the sequence number may be different when observed at the same time. Therefore, it should be put in advance, and delayed when creating a segment to be entered in the same number.

			// Get Duration of keyframe interval
			auto first_video_track = GetFirstTrackByType(cmn::MediaType::Video);
			if (first_video_track != nullptr)
			{
				double keyframe_interval_duration_ms = first_video_track->GetKeyframeIntervalDurationMs();
				double keyframe_interval_duration = keyframe_interval_duration_ms / 1000.0 * data_track->GetTimeBase().GetTimescale();
				timestamp_in_tb += std::ceil(keyframe_interval_duration);

				logti("SendDataFrame - %s/%s(%u) - timestamp: %lld tb, keyframe_interval_duration_ms: %f ms, keyframe_interval_duration: %f tb, keyframe_interval: %f, framerate: %f",
				GetApplicationName(), GetName().CStr(), GetId(), timestamp_in_tb, keyframe_interval_duration_ms, std::ceil(keyframe_interval_duration), first_video_track->GetKeyFrameInterval(), first_video_track->GetFrameRate());
			}
		}

		auto event_message = std::make_shared<MediaPacket>(GetMsid(),
															cmn::MediaType::Data,
															data_track->GetId(),
															frame, 
															timestamp_in_tb,
															timestamp_in_tb,
															duration,
															packet_flag,
															format,
															packet_type);
		event_message->SetHighPriority(urgent);

		// Mark as internal created packet
		// stream_actions.controller, mediarouter_event_generator, etc use this flag to identify internally created packets.
		event_message->SetInternalCreated(internal);
		
		return SendFrame(event_message);
	}

	bool Stream::SendDataFrame(int64_t timestamp, const cmn::BitstreamFormat &format, const cmn::PacketType &packet_type, const std::shared_ptr<ov::Data> &frame, bool urgent, bool internal, const MediaPacketFlag packet_flag)
	{
		return SendDataFrame(timestamp, -1, format, packet_type, frame, urgent, internal, packet_flag);
	}

	bool Stream::SendSubtitleFrame(const ov::String &label, int64_t timestamp_in_ms, int64_t duration_ms, const cmn::BitstreamFormat &format, const std::shared_ptr<ov::Data> &frame, bool urgent)
	{
		if (frame == nullptr)
		{
			return false;
		}

		auto subtitle_track = GetTrackByLabel(label);
		if (subtitle_track == nullptr)
		{
			logte("Subtitle label(%s) is not found. %s/%s(%u)", label.CStr(), GetApplicationName(), GetName().CStr(), GetId());
			return false;
		}

		if (timestamp_in_ms == -1)
		{
			timestamp_in_ms = GetCurrentTimestampMs();
			if (timestamp_in_ms == -1)
			{
				logte("Could not send subtitle frame. %s/%s(%u) - Media is not started yet", GetApplicationName(), GetName().CStr(), GetId());
				return false;
			}
		}

		auto timestamp_in_tb = static_cast<int64_t>(timestamp_in_ms * subtitle_track->GetTimeBase().GetTimescale() / 1000.0);
		auto duration_in_tb = static_cast<int64_t>(duration_ms * subtitle_track->GetTimeBase().GetTimescale() / 1000.0);

		auto subtitle_message = std::make_shared<MediaPacket>(GetMsid(),
															cmn::MediaType::Subtitle,
															subtitle_track->GetId(),
															frame, 
															timestamp_in_tb,
															timestamp_in_tb,
															duration_in_tb,
															MediaPacketFlag::Key,
															cmn::BitstreamFormat::WebVTT,
															cmn::PacketType::RAW);
		subtitle_message->SetHighPriority(urgent);
		
		return SendFrame(subtitle_message);
	}

	bool Stream::SendEvent(const std::shared_ptr<MediaEvent> &event)
	{
		if (event == nullptr)
		{
			return false;
		}

		ProcessEvent(event);

		// Forward
		auto data_track = GetFirstTrackByType(cmn::MediaType::Data);
		if (data_track == nullptr)
		{
			logte("Data track is not found. %s/%s(%u)", GetApplicationName(), GetName().CStr(), GetId());
			return false;
		}

		event->SetMsid(GetMsid());
		event->SetTrackId(data_track->GetId());
		event->SetPacketType(cmn::PacketType::EVENT);
		event->SetBitstreamFormat(cmn::BitstreamFormat::OVEN_EVENT);
		event->SetHighPriority(true);

		return SendFrame(event);
	}

	bool Stream::ProcessEvent(const std::shared_ptr<MediaEvent> &event)
	{
		if (event == nullptr)
		{
			return false;
		}

		switch (event->GetCommandType())
		{
			case EventCommand::Type::UpdateSubtitleLanguage:
			{
				auto command = event->GetCommand<EventCommandUpdateLanguage>();
				if (command != nullptr)
				{
					auto track = GetTrackByLabel(command->GetTrackLabel());
					if (track != nullptr)
					{
						auto old_language = track->GetLanguage();
						track->SetLanguage(command->GetLanguage());
						logtt("[%s/%s(%u)] Subtitle track language has been updated %s -> %s", GetApplicationName(), GetName().CStr(), GetId(), old_language.CStr(), track->GetLanguage().CStr());
					}
				}

				break;
			}
			default:
				// Do nothing
				break;
		}

		return true;
	}

	std::shared_ptr<const ov::Url> Stream::GetRequestedUrl() const
	{
		return _requested_url;
	}

	void Stream::SetRequestedUrl(const std::shared_ptr<ov::Url> &requested_url)
	{
		_requested_url = requested_url;
	}

	std::shared_ptr<const ov::Url> Stream::GetFinalUrl() const
	{
		return _final_url;
	}

	void Stream::SetFinalUrl(const std::shared_ptr<ov::Url> &final_url)
	{
		_final_url = final_url;
	}

	bool Stream::SendFrame(const std::shared_ptr<MediaPacket> &packet)
	{
		if (_application == nullptr)
		{
			return false;
		}

		if (packet->GetPacketType() == cmn::PacketType::Unknown)
		{
			logte("The packet type must be specified. %s/%s(%u)", GetApplicationName(), GetName().CStr(), GetId());
			return false;
		}

		if (packet->GetPacketType() != cmn::PacketType::OVT &&
			packet->GetBitstreamFormat() == cmn::BitstreamFormat::Unknown)
		{
			logte("The bitstream format must be specified. %s/%s(%u)", GetApplicationName(), GetName().CStr(), GetId());
			return false;
		}

		// Statistics
		MonitorInstance->IncreaseBytesIn(*GetSharedPtrAs<info::Stream>(), packet->GetDataLength());

		_last_pkt_received_time = std::chrono::system_clock::now();

		auto master_clock_track = GetMediaTrackByOrder(cmn::MediaType::Video, 0);
		if (master_clock_track == nullptr)
		{
			master_clock_track = GetMediaTrackByOrder(cmn::MediaType::Audio, 0);
		}

		if (master_clock_track != nullptr && packet->GetTrackId() == master_clock_track->GetId())
		{
			_last_media_timestamp_ms = packet->GetPts() / GetTrack(packet->GetTrackId())->GetTimeBase().GetTimescale() * 1000.0;
			_elapsed_from_last_media_timestamp.Restart();
		}

		return _application->SendFrame(GetSharedPtr(), packet);
	}

	bool Stream::SetState(State state)
	{
		// STOPPED state is only set by calling Stream::Stop()
		if (state == State::STOPPED)
		{
			return false;
		}

		_state = state;
		return true;
	}

	void Stream::ResetSourceStreamTimestamp()
	{
		// Set the last timestamp of the highest value of all tracks
		// In this algorithm, the timestamp of A or V jumps for synchronization.
		// But after testing with a variety of players, this is better.
		int64_t last_timestamp = std::numeric_limits<int64_t>::min();
		for (const auto &[track_id, timestamp] : _last_timestamp_us_map)
		{
			auto track = GetTrack(track_id);
			if (!track)
			{
				continue;
			}

			int64_t last_duration = _last_duration_us_map.find(track_id) != _last_duration_us_map.end() ? _last_duration_us_map[track_id] : 0;
			last_timestamp = std::max<int64_t>(timestamp + last_duration, last_timestamp);
		}	

		if (last_timestamp != std::numeric_limits<int64_t>::min())
		{
			_base_timestamp_us = last_timestamp;
		}

		// Initialized start timestamp
		_start_timestamp_us = -1LL;

		_source_timestamp_map.clear();
	}

	void Stream::RegisterRtpClock(uint32_t track_id, double clock_rate)
	{
		_rtp_lip_sync_clock.RegisterRtpClock(track_id, clock_rate);
	}

	void Stream::UpdateSenderReportTimestamp(uint32_t track_id, uint32_t msw, uint32_t lsw, uint32_t timestamp)
	{
		_rtp_lip_sync_clock.UpdateSenderReportTime(track_id, msw, lsw, timestamp);
	}

	bool Stream::AdjustRtpTimestamp(uint32_t track_id, int64_t timestamp, int64_t max_timestamp, int64_t &adjusted_timestamp)
	{
		// Make decision timestamp calculation method	
		if (_rtp_timestamp_method == RtpTimestampCalculationMethod::UNDER_DECISION)
		{
			if (GetDirectionType() == DirectionType::PULL)
			{
				// If this stream is type of PullStream, check the property of IgnoreRtcpSRTimestamp.
				auto stream = std::static_pointer_cast<pvd::PullStream>(GetSharedPtr());
				if(stream != nullptr)
				{
					auto props = stream->GetProperties();
					if(props != nullptr)
					{
						if(props->IsRtcpSRTimestampIgnored())
						{
							_rtp_timestamp_method = RtpTimestampCalculationMethod::SINGLE_DELTA;
						}
					}
				}
			}

			if (_rtp_timestamp_method == RtpTimestampCalculationMethod::UNDER_DECISION)
			{
				if ((GetMediaTrackCount(cmn::MediaType::Video) + GetMediaTrackCount(cmn::MediaType::Audio)) == 1)
				{
					logti("Since this stream has a single track, it computes PTS alone without RTCP SR.");
					_rtp_timestamp_method = RtpTimestampCalculationMethod::SINGLE_DELTA;
				}
				else if (_rtp_lip_sync_clock.IsEnabled() == true)
				{
					logti("Since this stream has received an RTCP SR, it counts the PTS with the SR.");
					_rtp_timestamp_method = RtpTimestampCalculationMethod::WITH_RTCP_SR;
				}
				// If it exceeds 5 seconds, it is calculated independently without RTCP SR.
				else if (_rtp_lip_sync_clock.IsEnabled() == false && _first_rtp_received_time.Elapsed() > 5000)
				{
					logtw("Since the RTCP SR was not received within 5 seconds, the PTS is calculated for each track without RTCP SR. (Lip-Sync may be out of sync)");
					_rtp_timestamp_method = RtpTimestampCalculationMethod::SINGLE_DELTA;
				}
				else if (_rtp_lip_sync_clock.IsEnabled() == false && _first_rtp_received_time.Elapsed() <= 5000)
				{
					// Wait for RTCP SR for 5 seconds
					if (_first_rtp_received_time.IsStart() == false)
					{
						logtw("Wait for RTCP SR for 5 seconds before starting the stream.");
						_first_rtp_received_time.Start();
					}
					return false; 
				}
			}
		}

		if (_rtp_timestamp_method == RtpTimestampCalculationMethod::WITH_RTCP_SR)
		{
			auto pts_base = _rtp_lip_sync_clock.CalcPTS(track_id, timestamp);
			if (pts_base.has_value() == false)
			{
				return false;
			}

			int64_t pts = pts_base.value();
			adjusted_timestamp = AdjustTimestampByBase(track_id, pts, pts, max_timestamp);
		}
		else if (_rtp_timestamp_method == RtpTimestampCalculationMethod::SINGLE_DELTA)
		{
			adjusted_timestamp = AdjustTimestampByDelta(track_id, timestamp, max_timestamp);
		}
		else
		{
			return false;
		}

		return true;
	}

	// Zero-based or SystemClock-based timestamp mode
	// This keeps the pts value of the input track (only the start value<base_timestamp> is different), meaning that this value can be used for A/V sync.
	// returns adjusted PTS and parameter PTS and DTS are also adjusted.
	int64_t Stream::AdjustTimestampByBase(uint32_t track_id, int64_t &pts, int64_t &dts, int64_t max_timestamp, int64_t duration)
	{
		const auto track = GetTrack(track_id);

		if (track == nullptr)
		{
			return -1LL;
		}

		constexpr const int64_t AV_TIME_BASE = 1000000;
		const auto wraparound_ts_threshold	 = max_timestamp / 2;
		const auto &timebase				 = track->GetTimeBase();
		const int64_t num_tb				 = timebase.GetNum();  // e.g., 1
		const int64_t den_tb				 = timebase.GetDen();  // e.g., 48000

		// NOTE: `track_scale` is originally supposed to be calculated as `den_tb/num_tb`,
		// but in `Rescale()`, to improve the accuracy of integer operations,
		// instead of dividing in `track_scale`, `num_tb` is multiplied in `us_scale`.
		const auto us_scale					 = AV_TIME_BASE * num_tb;
		const auto &track_scale				 = den_tb;	// An alias of `den_tb` for clarity

		// 1. Get the start timestamp and base timebase of this stream.
		if (_start_timestamp_us == -1LL)
		{
			_start_timestamp_us = Rescale(dts, us_scale, track_scale);

			// for debugging
			logtt("[%s/%s(%d)] Get start timestamp of stream. track:%u, ts:%" PRId64 " (%" PRId64 "/%" PRId64 ") (%" PRId64 " us)",
				  _application->GetVHostAppName().CStr(), GetName().CStr(), GetId(),
				  track_id,
				  dts, num_tb, den_tb,
				  _start_timestamp_us);
		}

		int64_t start_timestamp_tb = Rescale(_start_timestamp_us, track_scale, us_scale);

		// 2. Make the base timestamp
		if (_base_timestamp_us == -1)
		{
			switch (GetTimestampMode())
			{
				case TimestampMode::Original:
					_base_timestamp_us = Rescale(dts, us_scale, track_scale);  // Original timestamp starts from original PTS
					break;
				case TimestampMode::Auto:
				case TimestampMode::ZeroBased:
				default:
					_base_timestamp_us = 0;	 // Default to 0
					break;
			}
		}

		const int64_t base_ts_tb = Rescale(_base_timestamp_us, track_scale, us_scale);

		// 3. Calculate PTS/DTS (base_timestamp + (pts - start_timestamp))

		int64_t final_pts_tb	 = base_ts_tb + (pts - start_timestamp_tb);
		int64_t final_dts_tb	 = base_ts_tb + (dts - start_timestamp_tb);

		// 4. Check wrap around and adjust PTS/DTS

		// For PTS
		auto &origin_pts_map	 = _last_origin_ts_map[0];
		{
			// Initialize wraparound count for PTS
			auto &pts_wrap			 = _wraparound_count_map[0][track_id];

			// PTS is not sequential. Therefore, the PTS may wrap around and return again.
			const auto origin_pts_it = origin_pts_map.find(track_id);
			if (origin_pts_it != origin_pts_map.end())
			{
				// Check if wrap arounded or reverse wrap arounded
				const auto last_origin_pts = origin_pts_it->second;
				if ((last_origin_pts - pts) > wraparound_ts_threshold)
				{
					pts_wrap++;
					logti("[PTS] Wrap around detected. track:%d", track_id);
				}
				else if ((pts - last_origin_pts) > wraparound_ts_threshold)
				{
					if (pts_wrap > 0)
					{
						pts_wrap--;
						logti("[PTS] Reverse wrap around detected. It could be caused by b-frames. track:%d", track_id);
					}
				}
			}

			final_pts_tb += pts_wrap * max_timestamp;
		}

		// For DTS
		auto &origin_dts_map = _last_origin_ts_map[1];
		{
			// Initialize wraparound count for DTS
			auto &dts_wrap			 = _wraparound_count_map[1][track_id];

			const auto origin_dts_it = origin_dts_map.find(track_id);
			if (origin_dts_it != origin_dts_map.end())
			{
				const auto last_origin_dts = origin_dts_it->second;
				if ((last_origin_dts - dts) > wraparound_ts_threshold)
				{
					dts_wrap++;
					logti("[DTS] Wrap around detected. track:%d", track_id);
				}
			}

			final_dts_tb += dts_wrap * max_timestamp;
		}

#if 0
		// for debugging
		int64_t pts_s = rescale(final_pts_tb, 1 * tb_num, tb_den);
		int64_t dts_s = rescale(final_dts_tb, 1 * tb_num, tb_den);
		logti("[%s/%s(%d)] track:%3d, pts (in : %8lld -> out : %8lld (%8llds)), dts (in : %8lld -> out : %8lld (%8llds)), tb:%d/%d / starttime:%lld (%lld us), basetime:%lld (%lld us)",
				_application->GetVHostAppName().CStr(), GetName().CStr(), GetId(), track_id,
				pts, final_pts_tb, pts_s,
				dts, final_dts_tb, dts_s,
				track->GetTimeBase().GetNum(), track->GetTimeBase().GetDen(),
				start_timestamp_tb, _start_timestamp_us, base_ts_tb, _base_timestamp_us);
#endif

		// 5. Update last timestamp ( Managed in microseconds )
		origin_pts_map[track_id]		 = pts;
		origin_dts_map[track_id]		 = dts;

		_last_timestamp_us_map[track_id] = Rescale(final_dts_tb, us_scale, track_scale);
		_last_duration_us_map[track_id]	 = Rescale(duration, us_scale, track_scale);

		pts								 = final_pts_tb;
		dts								 = final_dts_tb;

		return pts;
	}

	int64_t Stream::GetBaseTimestamp(uint32_t track_id)
	{
		auto track = GetTrack(track_id);
		if (track == nullptr)
		{
			return -1LL;
		}

		int64_t base_timestamp = 0;
		if (_base_timestamp_us != -1)
		{
			base_timestamp = _base_timestamp_us;
		}

		auto base_timestamp_tb = (base_timestamp * track->GetTimeBase().GetTimescale() / 1000000);

		return base_timestamp_tb;
	}

	// This is a method of generating a PTS with an increment value (delta) when it cannot be used as a PTS because the start value of the timestamp is random like the RTP timestamp.
	int64_t Stream::AdjustTimestampByDelta(uint32_t track_id, int64_t timestamp, int64_t max_timestamp)
	{
		int64_t curr_timestamp;

		if (_last_timestamp_us_map.find(track_id) == _last_timestamp_us_map.end())
		{
			curr_timestamp = 0;
		}
		else
		{
			curr_timestamp = _last_timestamp_us_map[track_id];
		}

		auto delta = GetDeltaTimestamp(track_id, timestamp, max_timestamp);
		curr_timestamp += delta;

		_last_timestamp_us_map[track_id] = curr_timestamp;

		return curr_timestamp;
	}

	int64_t Stream::GetDeltaTimestamp(uint32_t track_id, int64_t timestamp, int64_t max_timestamp)
	{
		auto track = GetTrack(track_id);

		// First timestamp
		if (_source_timestamp_map.find(track_id) == _source_timestamp_map.end())
		{
			logtt("New track timestamp(%u) : curr(%lld)", track_id, timestamp);
			_source_timestamp_map[track_id] = timestamp;

			// Start with zero
			return 0;
		}

		int64_t delta = 0;

		// Wrap around or change source
		if (timestamp < _source_timestamp_map[track_id])
		{
			// If the last timestamp exceeds 99.99%, it is judged to be wrapped around.
			if (_source_timestamp_map[track_id] > ((double)max_timestamp * 99.99) / 100)
			{
				logtt("Wrapped around(%u) : last(%lld) curr(%lld)", track_id, _source_timestamp_map[track_id], timestamp);
				delta = (max_timestamp - _source_timestamp_map[track_id]) + timestamp;
			}
			// Otherwise, the source might be changed. (restarted)
			else
			{
				logtt("Source changed(%u) : last(%lld) curr(%lld)", track_id, _source_timestamp_map[track_id], timestamp);
				delta = 0;
			}
		}
		else
		{
			delta = timestamp - _source_timestamp_map[track_id];
		}

		_source_timestamp_map[track_id] = timestamp;
		return delta;
	}

	// Increase MSID and notify the application of the stream update
	bool Stream::UpdateStream()
	{
		if (_application == nullptr)
		{
			return false;
		}

		if (_application->UpdateStream(GetSharedPtr()) == false)
		{
			return false;
		}

		ResetSourceStreamTimestamp();
		SetMsid(GetMsid() + 1);

		logti("%s/%s(%u) has been updated stream", GetApplicationName(), GetName().CStr(), GetId());
		logti("%s", GetInfoString().CStr());

		return true;
	}
}  // namespace pvd