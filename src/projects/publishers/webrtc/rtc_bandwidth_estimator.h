//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/rtp_rtcp/rtcp_info/remb.h>
#include <modules/rtp_rtcp/rtcp_info/transport_cc.h>
#include <modules/rtp_rtcp/rtp_packet.h>

class RtcBandwidthEstimatorSignal
{
public:
	enum class State
	{
		UnderUse,
		Stable,
		OverUse
	};

	void SetState(State state)
	{
		_state = state;
	}

	State GetState() const
	{
		return _state;
	}

private:
	State _state = State::Stable;
};

class RtcBandwidthEstimatorObserver : public ov::EnableSharedFromThis<RtcBandwidthEstimatorObserver>
{
public:
	virtual void OnSignal(std::shared_ptr<RtcBandwidthEstimatorSignal> &signal) = 0;

	virtual uint64_t GetCurrentBitrateBps() const								= 0;
	virtual uint64_t GetNextHigherBitrateBps() const							= 0;  // For UnderUse state
};

static constexpr uint32_t kMaxRtpPacketHistory = 65535;
class RtcBandwidthEstimator
{
public:
	enum class Mechanism
	{
		TransportCc,
		Remb
	};

	static std::shared_ptr<RtcBandwidthEstimator> Create(Mechanism mechanism, const std::shared_ptr<RtcBandwidthEstimatorObserver> &observer);
	void Release();

	explicit RtcBandwidthEstimator(Mechanism mechanism, const std::shared_ptr<RtcBandwidthEstimatorObserver> &observer);
	virtual ~RtcBandwidthEstimator();

	void OnRtpSent(uint16_t wide_seq_no, const std::shared_ptr<const RtpPacket> &rtp_packet);
	void OnTransportCc(const std::shared_ptr<const TransportCc> &transport_cc);
	void OnRemb(const std::shared_ptr<const REMB> &remb);

	std::shared_ptr<RtcBandwidthEstimatorSignal> GetLastSignal() const;

	bool ProcessTransportCc();

private:
	Mechanism _mechanism = Mechanism::Remb;
	std::shared_ptr<RtcBandwidthEstimatorObserver> _observer;
	std::shared_ptr<RtcBandwidthEstimatorSignal> _last_signal;

	void Signal(RtcBandwidthEstimatorSignal::State state);


	struct RtpHistory
	{
		uint16_t wide_sequence_number = 0;
		uint8_t payload_type		  = 0;
		uint32_t timestamp			  = 0;
		size_t packet_size			  = 0;
		std::chrono::steady_clock::time_point sent_time;
	};
	// wide_sequence_number -> RtpHistory
	std::vector<RtpHistory> _rtp_history;
	std::shared_mutex _rtp_history_guard;

	uint32_t GetRtpHistoryIndex(uint16_t wide_seq_no) const;

	struct TransportCCFeedback
	{
		uint16_t wide_seq_no = 0;
		bool received = false;
		int64_t arrival_time_ms = 0;
	};
	std::deque<TransportCCFeedback> _tcc_feedbacks;
	std::shared_mutex _tcc_feedbacks_guard;

	struct FrameStats
	{
		size_t total_bytes = 0;
		int packet_total_count = 0;
		int packet_lost_count = 0;

		int64_t first_send_ms = std::numeric_limits<int64_t>::max();
		int64_t first_recv_ms = std::numeric_limits<int64_t>::max();
		int64_t last_recv_ms  = std::numeric_limits<int64_t>::min();
	};
	std::map<uint32_t, FrameStats> _frame_stats; // timestamp -> FrameStats
	
	// TrendLine
	struct FrameSample 
	{
		int64_t send_time_ms;
		int64_t arrival_time_ms;
		int64_t delay_variation_ms;
	};
	std::deque<FrameSample> _trendline_window;
	FrameStats _prev_frame_stats;
	double _trendline_slope = 0.0;
	double _ema_trendline_slope = 0.0;
	double UpdateTrendLine(const FrameStats &current_frame_stats);

	bool IsNewerRtpTimestamp(uint32_t base, uint32_t comp) const;
	double CalculateTrendlineSlope() const;

	// Packet loss tracking
	double _ema_packet_loss_ratio = 0.0;
	double UpdatePacketLoss(const FrameStats& stat);
	
	// Bitrate Estimation
	double _ema_bitrate_bps = 0.0;
	double UpdateEstimatedBitrateBps(const FrameStats& stat);

	// Make decision
	static constexpr int64_t kUpgradeProbationPeriodMs = 3000;
	static constexpr int64_t kInitialActiveProbingIntervalMs = 10000;
	static constexpr int64_t kMaxActiveProbingIntervalMs = 120000;
	int64_t _active_probing_interval_ms = kInitialActiveProbingIntervalMs;
	std::chrono::steady_clock::time_point _last_active_probing_failure_time;

	static constexpr double kOverUseTriggerCount = 3;
	static constexpr double kOverUseSlopeThreshold = 0.4;
    static constexpr double kOverUseLossThreshold = 0.05; // 5%
	static constexpr int64_t kOverUseCoolDownMs = 2000;
    
    static constexpr double kUnderUseSlopeThreshold = 0.2;
    static constexpr double kUnderUseLossThreshold = 0.01; // 1%

	int _overuse_detect_count = 0;

	enum class InternalState {
        Neutral, // Stable but not optimal
        Cooldown, // OverUse recovery period
		Optimal, // Can be upgraded
		Probing // Trying to upgrade
    };
    InternalState _state = InternalState::Neutral;
	std::chrono::steady_clock::time_point _state_entry_time;

	InternalState GetState() const;
	std::chrono::steady_clock::time_point GetStateEntryTime() const;
	int64_t GetTimeInStateMs() const;

	void SetState(InternalState state);

	void DetermineNetworkState(double trend_slope, double loss_ratio, double current_bitrate_bps);
};