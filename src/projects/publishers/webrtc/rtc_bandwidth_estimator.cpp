//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "rtc_bandwidth_estimator.h"
#include "rtc_private.h"

std::shared_ptr<RtcBandwidthEstimator> RtcBandwidthEstimator::Create(Mechanism mechanism, const std::shared_ptr<RtcBandwidthEstimatorObserver> &observer)
{
	return std::make_shared<RtcBandwidthEstimator>(mechanism, observer);
}

void RtcBandwidthEstimator::Release()
{
	_observer.reset();
	_last_signal.reset();
}

RtcBandwidthEstimator::RtcBandwidthEstimator(Mechanism mechanism, const std::shared_ptr<RtcBandwidthEstimatorObserver> &observer)
{
	_mechanism = mechanism;
	_observer  = observer;

	_rtp_history.resize(kMaxRtpPacketHistory);
}

RtcBandwidthEstimator::~RtcBandwidthEstimator()
{
}

void RtcBandwidthEstimator::Signal(RtcBandwidthEstimatorSignal::State state)
{
	if (_observer == nullptr)
	{
		return;
	}

	auto signal = std::make_shared<RtcBandwidthEstimatorSignal>();
	signal->SetState(state);

	_last_signal = signal;

	_observer->OnSignal(signal);
}

void RtcBandwidthEstimator::OnRtpSent(uint16_t wide_seq_no, const std::shared_ptr<const RtpPacket> &rtp_packet)
{
	std::lock_guard<std::shared_mutex> lock(_rtp_history_guard);
	_rtp_history[wide_seq_no % kMaxRtpPacketHistory] = RtpHistory{
		.wide_sequence_number = wide_seq_no,
		.payload_type = rtp_packet->PayloadType(),
		.timestamp = rtp_packet->Timestamp(),
		.packet_size = rtp_packet->GetDataLength(),
		.sent_time = std::chrono::steady_clock::now()
	};
}

void RtcBandwidthEstimator::OnTransportCc(const std::shared_ptr<const TransportCc> &transport_cc)
{
	if (_observer == nullptr)
	{
		return;
	}

	{
		std::lock_guard<std::shared_mutex> lock(_tcc_feedbacks_guard);

		if (transport_cc == nullptr || transport_cc->GetPacketStatusCount() == 0)
		{
			return;
		}

		// Process each PacketFeedbackInfo
		for (uint32_t i = 0; i < transport_cc->GetPacketStatusCount(); i++)
		{
			auto packet_feedback = transport_cc->GetPacketFeedbackInfo(i);
			if (packet_feedback == nullptr)
			{
				continue;
			}

			TransportCCFeedback feedback;
			feedback.wide_seq_no = packet_feedback->_wide_sequence_number;
			feedback.received = packet_feedback->_received;
			feedback.arrival_time_ms = packet_feedback->_arrival_time_ms;
			_tcc_feedbacks.push_back(feedback);
		}
	}

	// TODO(Getroot) : Need to consider whether to run ProcessTransportCc in a separate thread pool
	ProcessTransportCc();
}

void RtcBandwidthEstimator::OnRemb(const std::shared_ptr<const REMB> &remb)
{
	if (_observer == nullptr || remb == nullptr)
	{
		return;
	}

	double remb_bitrate_bps = static_cast<double>(remb->GetBitrateBps());
	double current_bitrate_bps = static_cast<double>(_observer->GetCurrentBitrateBps());
	double next_higher_bitrate_bps = static_cast<double>(_observer->GetNextHigherBitrateBps());

	double simulated_slope = 0.0;
	if (remb_bitrate_bps < current_bitrate_bps * 0.97) // 3% Margin
    {
		if (GetState() == InternalState::Probing)
        {
			// Ramping up
			simulated_slope = 0.1;
		}
		else
		{
			// Over Use
        	simulated_slope = 1.0; 
		}
    }
    else if (next_higher_bitrate_bps > 0 && remb_bitrate_bps >= next_higher_bitrate_bps)
    {
		// Under Use
        simulated_slope = 0.01; 
    }
    else
    {
        // Potentially Under Use
        simulated_slope = 0.1;
    }

    double current_loss_ratio = 0.0;

	logtd("REMB received: Estimated Bandwidth=%.2f Mbps, CurrentBitrate=%.2f Mbps, NextHigherBitrate=%.2f Mbps, SimulatedSlope=%.6f", remb_bitrate_bps / 1e6, current_bitrate_bps / 1e6, next_higher_bitrate_bps / 1e6, simulated_slope);

    DetermineNetworkState(simulated_slope, current_loss_ratio, remb_bitrate_bps);
}

std::shared_ptr<RtcBandwidthEstimatorSignal> RtcBandwidthEstimator::GetLastSignal() const
{
	return _last_signal;
}

bool RtcBandwidthEstimator::ProcessTransportCc()
{
	std::deque<TransportCCFeedback> batch_feedbacks;

	{
		std::lock_guard<std::shared_mutex> lock(_tcc_feedbacks_guard);
		batch_feedbacks.swap(_tcc_feedbacks);
	}

	if (batch_feedbacks.empty())
	{
		return true;
	}

	bool has_update = false;
	uint32_t lastest_timestamp = 0;
	for (const auto &feedback : batch_feedbacks)
	{
		// Find RTP history
		RtpHistory rtp_history;
		{
			std::shared_lock<std::shared_mutex> lock(_rtp_history_guard);
			const auto &item = _rtp_history[GetRtpHistoryIndex(feedback.wide_seq_no)];
			if (item.wide_sequence_number != feedback.wide_seq_no ||
				item.sent_time.time_since_epoch().count() == 0)
			{
				logtw("Cannot find RTP history for wide_seq_no(%u)", feedback.wide_seq_no);
				continue;
			}

			rtp_history = item;
		}

		FrameStats &frame_stats = _frame_stats[rtp_history.timestamp];
		frame_stats.total_bytes += rtp_history.packet_size;
		frame_stats.packet_total_count += 1;

		if (feedback.received == false)
		{
			frame_stats.packet_lost_count += 1;
		}
		else
		{
			if (feedback.arrival_time_ms < frame_stats.first_recv_ms)
			{
				frame_stats.first_recv_ms = feedback.arrival_time_ms;
			}
			if (feedback.arrival_time_ms > frame_stats.last_recv_ms)
			{
				frame_stats.last_recv_ms = feedback.arrival_time_ms;
			}
		}

		auto sent_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rtp_history.sent_time.time_since_epoch()).count();
		if (sent_time_ms < frame_stats.first_send_ms)
		{
			frame_stats.first_send_ms = sent_time_ms;
		}
		
		if (has_update == false || IsNewerRtpTimestamp(rtp_history.timestamp, lastest_timestamp))
		{
			lastest_timestamp = rtp_history.timestamp;
			has_update = true;
		}
	}

	if (has_update == false)
	{
		return true;
	}

	auto it = _frame_stats.begin();
	while (it != _frame_stats.end())
	{
		// Keep the latest frame stats since it may be still updated
		if (it->first == lastest_timestamp)
		{
			++it;
		}
		else
		{
			auto current_slope = UpdateTrendLine(it->second);
			auto current_loss_ratio = UpdatePacketLoss(it->second);
			auto current_bitrate = UpdateEstimatedBitrateBps(it->second);
			
			DetermineNetworkState(current_slope, current_loss_ratio, current_bitrate);

			it =  _frame_stats.erase(it);
		}
	}

	return true;
}

uint32_t RtcBandwidthEstimator::GetRtpHistoryIndex(uint16_t wide_seq_no) const
{
	return wide_seq_no % kMaxRtpPacketHistory;
}

bool RtcBandwidthEstimator::IsNewerRtpTimestamp(uint32_t timestamp, uint32_t prev_timestamp) const
{
	return (timestamp != prev_timestamp) && (static_cast<uint32_t>(timestamp - prev_timestamp) < 0x80000000);
}

double RtcBandwidthEstimator::UpdateTrendLine(const FrameStats &current_frame_stats)
{
	static constexpr int64_t kTrendWindowSize = 30;
	static constexpr int64_t kAbsMaxDelayMs = 500; // 500 ms
	if (_prev_frame_stats.packet_total_count == 0)
	{
		_prev_frame_stats = current_frame_stats;
		return _trendline_slope;
	}

	int64_t send_delta_ms = current_frame_stats.first_send_ms - _prev_frame_stats.first_send_ms;
	int64_t recv_delta_ms = current_frame_stats.first_recv_ms - _prev_frame_stats.first_recv_ms;
	int64_t delay_ms = recv_delta_ms - send_delta_ms;

	_prev_frame_stats = current_frame_stats;

	// Accumulate delay
	int64_t acc_delay_ms = delay_ms;
	if (_trendline_window.empty() == false)
	{
		acc_delay_ms = _trendline_window.back().delay_variation_ms + delay_ms;
	}

	_trendline_window.push_back(FrameSample{
		.send_time_ms = current_frame_stats.first_send_ms,
		.arrival_time_ms = current_frame_stats.last_recv_ms,
		.delay_variation_ms =  acc_delay_ms
	});

	if (_trendline_window.size() > kTrendWindowSize)
	{
		_trendline_window.pop_front();
	}

	// To detect cases that steadily increase with a low slope
	if (acc_delay_ms > kAbsMaxDelayMs)
	{
		return 1.0; // Indicate over-use
	}

	if (_trendline_window.size() < kTrendWindowSize / 2)
	{
		return _trendline_slope;
	}

	_trendline_slope = CalculateTrendlineSlope();
	
	logtd("Trendline curr_delay: %" PRId64 " ms, acc_delay: %" PRId64 " ms, slope: %.6f", delay_ms, acc_delay_ms, _trendline_slope);

	return _trendline_slope;
}

double RtcBandwidthEstimator::CalculateTrendlineSlope() const
{
	size_t N = _trendline_window.size();
	if (N < 2)
	{
		return 0.0;
	}

	double sum_x = 0.0;
	double sum_y = 0.0;
	double sum_xy = 0.0;
	double sum_x2 = 0.0;

	int64_t start_x = _trendline_window.front().arrival_time_ms;
	for (const auto &sample : _trendline_window)
	{
		double x = static_cast<double>(sample.arrival_time_ms - start_x);
		double y = static_cast<double>(sample.delay_variation_ms);

		sum_x += x;
		sum_y += y;
		sum_xy += x * y;
		sum_x2 += x * x;
	}

	double numerator = (N * sum_xy) - (sum_x * sum_y);
	double denominator = (N * sum_x2) - (sum_x * sum_x);

	if (std::abs(denominator) < 1e-9)
	{
		return 0.0;
	}

	return numerator / denominator;
}

double RtcBandwidthEstimator::UpdatePacketLoss(const FrameStats& stat)
{
	static constexpr double kAlpha = 0.8;
	
	if (stat.packet_total_count == 0)
	{
		return _ema_packet_loss_ratio;
	}

	double current_loss = static_cast<double>(stat.packet_lost_count) / static_cast<double>(stat.packet_total_count);

	// Calc EMA packet loss ratio
	if (_ema_packet_loss_ratio == 0.0)
	{
		_ema_packet_loss_ratio = current_loss;
	}
	else
	{
		_ema_packet_loss_ratio = (kAlpha * _ema_packet_loss_ratio) + ((1.0 - kAlpha) * current_loss);
	}

	logtd("Packet Loss: current=%.2f%%, ema=%.2f%%", current_loss * 100.0, _ema_packet_loss_ratio * 100.0);

	return _ema_packet_loss_ratio;
}

double RtcBandwidthEstimator::UpdateEstimatedBitrateBps(const FrameStats& stat) 
{
	static constexpr int64_t kMinSizeForEstimation = 10000; // bytes
	if (stat.total_bytes <= kMinSizeForEstimation)
	{
		// Not enough data, keep previous value
		return _ema_bitrate_bps;
	}

	int64_t duration_ms = stat.last_recv_ms - stat.first_recv_ms;
	if (duration_ms <= 0)
	{
		// Invalid duration, keep previous value
		return _ema_bitrate_bps;
	}

	double bitrate_bps = (static_cast<double>(stat.total_bytes) * 8.0 * 1000.0) / static_cast<double>(duration_ms);

	// Calc EMA bitrate
	static constexpr double kAlpha = 0.8;
	if (_ema_bitrate_bps == 0.0)
	{
		_ema_bitrate_bps = bitrate_bps;
	}
	else
	{
		_ema_bitrate_bps = (kAlpha * _ema_bitrate_bps) + ((1.0 - kAlpha) * bitrate_bps);
	}

	logtd("Estimated Bitrate: current=%.2f Mbps, ema=%.2f Mbps", bitrate_bps / 1e6, _ema_bitrate_bps / 1e6);

	return _ema_bitrate_bps;
}

void RtcBandwidthEstimator::DetermineNetworkState(double trend_slope, double loss_ratio, double estimated_bandwidth_bps)
{
	// _ema_trendline_slope is to check UnderUse condition more stably
	// _trendline_slope is to check OverUse condition responsively
	static constexpr double kAlpha = 0.8;
	if (_ema_trendline_slope == 0.0)
	{
		_ema_trendline_slope = trend_slope;
	}
	else
	{
		_ema_trendline_slope = (kAlpha * _ema_trendline_slope) + ((1.0 - kAlpha) * trend_slope);
	}

	logtd("DetermineNetworkState: slope=%.6f, emaSlope=%.6f, loss=%.2f%%, estimatedBandwidth=%.2f Mbps", trend_slope, _ema_trendline_slope, loss_ratio * 100.0, estimated_bandwidth_bps / 1e6);
	
	auto now = std::chrono::steady_clock::now();
	bool is_congested = false; 

	if (GetState() == InternalState::Cooldown)
	{
		auto time_in_cooldown = GetTimeInStateMs();
		if (time_in_cooldown >= kOverUseCoolDownMs)
		{
			SetState(InternalState::Neutral);
		}
		else
		{
			// Stay in Cooldown
			logtd("Network State: In Cooldown. Elapsed=%" PRId64 " ms", time_in_cooldown);
			return;
		}
	}

	if (trend_slope > kOverUseSlopeThreshold || _ema_trendline_slope > kOverUseSlopeThreshold || loss_ratio > kOverUseLossThreshold)
	{
		if (loss_ratio > 0.1) // 10% Packet Loss is a strong signal of congestion
		{
			// Immediate over-use if packet loss is too high
			is_congested = true;
		}

		_overuse_detect_count ++;
		if (_overuse_detect_count >= kOverUseTriggerCount)
		{
			is_congested = true;
			_overuse_detect_count = 0;
		}
		else
		{
			return;
		}
	}
	else
	{
		_overuse_detect_count = 0;
		is_congested = false;
	}

	if (is_congested == true)
	{
		// Backoff
		if (GetState() == InternalState::Probing)
		{
			_active_probing_interval_ms = std::min(_active_probing_interval_ms * 2, kMaxActiveProbingIntervalMs);
			_last_active_probing_failure_time = now;
			logtd("Network State: OverUse detected during Probing. Backoff to Neutral. New ActiveProbingInterval=%" PRId64 " ms", _active_probing_interval_ms);
		}

		SetState(InternalState::Cooldown);

		logtd("Network State: OverUse detected. Slope=%.6f, emaSlope=%.6f, Loss=%.2f%%, Estimated Bandwidth=%.2f Mbps", trend_slope, _ema_trendline_slope, loss_ratio * 100.0, estimated_bandwidth_bps / 1e6);

		Signal(RtcBandwidthEstimatorSignal::State::OverUse);

		return;
	}

	if (GetState() == InternalState::Probing)
	{
		auto time_in_probing_ms = GetTimeInStateMs();
		if (time_in_probing_ms >= kUpgradeProbationPeriodMs)
		{
			// Successful upgrade
			_active_probing_interval_ms = kInitialActiveProbingIntervalMs;
			SetState(InternalState::Neutral);

			logtd("Network State: Upgrade successful after probing. TimeInProbing=%" PRId64 " ms", time_in_probing_ms);

			Signal(RtcBandwidthEstimatorSignal::State::Stable);
			return;
		}
		else
		{
			// Still in probing
			logtd("Network State: In Probing. TimeInProbing=%" PRId64 " ms", time_in_probing_ms);
			return;
		}
	}

	auto next_higher_bitrate_bps = (_observer) ? _observer->GetNextHigherBitrateBps() : 0.0;
	if (next_higher_bitrate_bps == 0.0)
	{
		SetState(InternalState::Neutral);
		Signal(RtcBandwidthEstimatorSignal::State::Stable);
		logtd("Network State: Stable but no higher bitrate available. Slope=%.6f, Loss=%.2f%%, Estimated Bandwidth=%.2f Mbps", trend_slope, loss_ratio * 100.0, estimated_bandwidth_bps / 1e6);

		return;
	}

	bool is_optimal = (_ema_trendline_slope < kUnderUseSlopeThreshold) && (loss_ratio < kUnderUseLossThreshold);
	if (is_optimal == false)
	{
		SetState(InternalState::Neutral);
		Signal(RtcBandwidthEstimatorSignal::State::Stable);
		logtd("Network State: Stable but not optimal. slope=%.6f, emaSlope=%.6f, Loss=%.2f%%, Estimated Bandwidth=%.2f Mbps", trend_slope, _ema_trendline_slope, loss_ratio * 100.0, estimated_bandwidth_bps / 1e6);

		return;
	}
	else
	{
		SetState(InternalState::Optimal);
	}

	// This does not work well. estimated_bandwidth_bps is too inaccurate, even after EMA filtering.
	// if (estimated_bandwidth_bps > next_higher_bitrate_bps)
	// {
	// 	logti("Network State: Probing for upgrade. emaSlope=%.6f, Loss=%.2f%%, Estimated Bandwidth=%.2f Mbps, NextHigherBitrate=%.2f Mbps", _ema_trendline_slope, loss_ratio * 100.0, estimated_bandwidth_bps / 1e6, next_higher_bitrate_bps / 1e6);

	// 	// Can upgrade now
	// 	SetState(InternalState::Probing);
	// 	Signal(RtcBandwidthEstimatorSignal::State::UnderUse);

	// 	_active_probing_interval_ms = kInitialActiveProbingIntervalMs;

	// 	return;
	// }

	// Try active probing if stayed in Optimal for a while
	int64_t time_in_optimal_ms = GetTimeInStateMs();
	int64_t time_since_failure_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_active_probing_failure_time).count();
	if (time_in_optimal_ms >= _active_probing_interval_ms && time_since_failure_ms >= _active_probing_interval_ms)
	{
		SetState(InternalState::Probing);
		Signal(RtcBandwidthEstimatorSignal::State::UnderUse);

		logtd("Network State: Active Probing for upgrade. TimeInOptimal=%" PRId64 " ms, SinceLastFailure=%" PRId64 " ms, emaSlope=%.6f, Loss=%.2f%%, Bitrate=%.2f Mbps", 
			time_in_optimal_ms, time_since_failure_ms, _ema_trendline_slope, loss_ratio * 100.0, estimated_bandwidth_bps / 1e6);

		return;
	}

	Signal(RtcBandwidthEstimatorSignal::State::Stable);
	logtd("Network State: Optimal but waiting to upgrade. TimeInOptimal=%" PRId64 " ms, emaSlope=%.6f, Loss=%.2f%%, Bitrate=%.2f Mbps", time_in_optimal_ms, _ema_trendline_slope, loss_ratio * 100.0, estimated_bandwidth_bps / 1e6);
}

RtcBandwidthEstimator::InternalState RtcBandwidthEstimator::GetState() const
{
	return _state;
}

std::chrono::steady_clock::time_point RtcBandwidthEstimator::GetStateEntryTime() const
{
	return _state_entry_time;
}

int64_t RtcBandwidthEstimator::GetTimeInStateMs() const
{
	auto now = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now - _state_entry_time).count();
}

void RtcBandwidthEstimator::SetState(RtcBandwidthEstimator::InternalState state)
{
	if (_state == state)
	{
		return;
	}
	_state = state;
	_state_entry_time = std::chrono::steady_clock::now();
}