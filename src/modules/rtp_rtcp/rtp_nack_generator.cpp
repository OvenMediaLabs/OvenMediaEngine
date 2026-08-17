#include "rtp_nack_generator.h"

#include <algorithm>
#include <cmath>

#define OV_LOG_TAG "RtpNack"

RtpNackGenerator::RtpNackGenerator(uint32_t track_id, uint32_t media_ssrc, uint32_t max_hold_ms)
	: _track_id(track_id), _media_ssrc(media_ssrc), _hold_max_ms(max_hold_ms)
{
}

std::optional<uint32_t> RtpNackGenerator::ExtendSeq(uint16_t seq) const
{
	if (_initialized == false)
	{
		return static_cast<uint32_t>(seq);
	}

	uint16_t last_seq = static_cast<uint16_t>(_newest_extended & 0xFFFF);
	int16_t diff = static_cast<int16_t>(seq - last_seq);

	int64_t extended = static_cast<int64_t>(_newest_extended) + diff;
	if (extended < 0)
	{
		// Very old packet (predates the first packet we ever saw on this track).
		return std::nullopt;
	}

	return static_cast<uint32_t>(extended);
}

void RtpNackGenerator::OnPacketReceived(uint16_t seq)
{
	ov::LockGuard<ov::Mutex> lock(_lock);
	auto now = std::chrono::steady_clock::now();

	_received_total++;

	if (_initialized == false)
	{
		_initialized = true;
		_newest_extended = static_cast<uint32_t>(seq);
		_expected_next = static_cast<uint32_t>(seq) + 1;
		_last_stats_log_at = now;
		return;
	}

	auto extended_opt = ExtendSeq(seq);
	if (extended_opt.has_value() == false)
	{
		// Pre-track-start packet; nothing to do.
		return;
	}
	uint32_t extended = *extended_opt;

	if (extended >= _expected_next)
	{
		// In-order or new gap.
		if (extended > _expected_next)
		{
			uint32_t gap_size = extended - _expected_next;
			logtd("Gap detected track(%u) ssrc(%u) missing [%u..%u] (%u packets)",
				  _track_id, _media_ssrc,
				  static_cast<uint16_t>(_expected_next & 0xFFFF),
				  static_cast<uint16_t>((extended - 1) & 0xFFFF),
				  gap_size);

			uint32_t inserted = 0;
			for (uint32_t s = _expected_next; s < extended; s++)
			{
				if (_pending.size() >= MAX_PENDING)
				{
					uint32_t skipped = (extended - _expected_next) - inserted;
					logtw("NACK pending full track(%u) ssrc(%u): skipping %u of %u gap seqs "
						  "(pending cap %zu reached). Those seqs will not be NACK'd.",
						  _track_id, _media_ssrc, skipped, gap_size, MAX_PENDING);
					break;
				}

				// first_nack_at / last_nack_at / retry_count are stamped
				// when BuildPendingNack fires the initial NACK.
				PendingEntry entry;
				entry.inserted_at = now;
				_pending.emplace(s, entry);
				inserted++;
			}
		}

		_expected_next = extended + 1;
		_newest_extended = extended;
	}
	else
	{
		// Late or RTX-recovered packet.
		auto it = _pending.find(extended);
		if (it != _pending.end())
		{
			auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							  now - it->second.inserted_at)
							  .count();

			if (it->second.retry_count == 0)
			{
				// Filled within dwell — natural reorder, no NACK was sent.
				logtd("Reorder absorbed track(%u) ssrc(%u) seq(%u) age(%ldms) — no NACK sent",
					  _track_id, _media_ssrc, static_cast<uint16_t>(extended & 0xFFFF), age_ms);
			}
			else
			{
				// NACK was already fired; this is the late arrival.
				// Could be the RTX response OR the original packet finally
				// reordering through — generator can't distinguish, and
				// doesn't need to (pending is cleared either way).
				auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
									  now - it->second.first_nack_at)
									  .count();
				logtd("Late seq recovered track(%u) ssrc(%u) seq(%u) latency(%ldms) retries(%u)",
					  _track_id, _media_ssrc, static_cast<uint16_t>(extended & 0xFFFF),
					  latency_ms, it->second.retry_count);

				// Karn algorithm: only sample latency when exactly one NACK was
				// sent for this seq; retransmits introduce ambiguity.
				if (it->second.retry_count == 1)
				{
					UpdateLatencyStats(static_cast<double>(latency_ms), now);
				}
				_recovered_total++;
			}
			_pending.erase(it);
		}
	}

	DiscardStale(now);
	LogPeriodicStats(now);
}

std::vector<uint16_t> RtpNackGenerator::BuildPendingNack()
{
	ov::LockGuard<ov::Mutex> lock(_lock);
	auto now = std::chrono::steady_clock::now();

	DiscardStale(now);

	auto retry_interval = std::chrono::milliseconds(static_cast<int64_t>(_ewma_ms));
	auto dwell = std::chrono::milliseconds(INITIAL_NACK_DWELL_MS);

	std::vector<uint16_t> ids;
	size_t initial_count = 0;
	size_t retry_count_total = 0;
	for (auto &kv : _pending)
	{
		auto &entry = kv.second;

		if (entry.retry_count == 0)
		{
			// Wait a short dwell before the first NACK so brief out-of-order
			// delivery (seq 102 arriving before 101) clears itself without
			// triggering a spurious NACK + RTX round-trip.
			if ((now - entry.inserted_at) < dwell)
			{
				continue;
			}
			ids.push_back(static_cast<uint16_t>(kv.first & 0xFFFF));
			entry.first_nack_at = now;
			entry.last_nack_at = now;
			entry.retry_count = 1;
			initial_count++;
		}
		else if ((now - entry.last_nack_at) >= retry_interval)
		{
			// Retry every RTT until the jitter buffer ends the entry (via
			// DropPendingUpTo) or MAX_AGE_MS absolute cap fires.
			ids.push_back(static_cast<uint16_t>(kv.first & 0xFFFF));
			entry.last_nack_at = now;
			entry.retry_count++;
			retry_count_total++;
		}
	}

	_nacks_sent_total += ids.size();

	if (ids.empty() == false)
	{
		logtd("Fire NACK track(%u) ssrc(%u) total(%zu) initial(%zu) retry(%zu) pending(%zu) hold(%ums)",
			  _track_id, _media_ssrc, ids.size(), initial_count, retry_count_total, _pending.size(),
			  GetRecommendedHoldMsInternal());
	}

	return ids;
}

uint32_t RtpNackGenerator::GetRecommendedHoldMs() const
{
	ov::LockGuard<ov::Mutex> lock(_lock);
	return GetRecommendedHoldMsInternal();
}

uint32_t RtpNackGenerator::GetRecommendedHoldMsInternal() const
{
	// Use real EWMA when we have a fresh sample; otherwise fall back to the
	// initial RTT guess (same source used to seed _ewma_ms for retry interval).
	double ewma = INITIAL_RTT_GUESS_MS;
	double dev = 0.0;
	if (_stats_initialized)
	{
		auto since_last_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
								 std::chrono::steady_clock::now() - _last_sample_at)
								 .count();
		if (since_last_ms <= STATS_DECAY_MS)
		{
			ewma = _ewma_ms;
			dev = _ewma_dev_ms;
		}
	}

	// Hold long enough for MAX_NACK_RETRIES rounds: each NACK fires every
	// `ewma` and its RTX answer arrives one `ewma` later, so N rounds
	// complete by N * ewma after the first NACK. `dev` absorbs the variance
	// the round-trip sample already captures.
	double hold = INITIAL_NACK_DWELL_MS
				+ MAX_NACK_RETRIES * ewma
				+ EWMA_DEV_MULTIPLIER * dev;
	if (hold < HOLD_MIN_MS) hold = HOLD_MIN_MS;
	if (hold > _hold_max_ms) hold = _hold_max_ms;

	return static_cast<uint32_t>(hold);
}

std::optional<uint16_t> RtpNackGenerator::GetLowestPendingSeq() const
{
	ov::LockGuard<ov::Mutex> lock(_lock);
	if (_pending.empty())
	{
		return std::nullopt;
	}
	return static_cast<uint16_t>(_pending.begin()->first & 0xFFFF);
}

void RtpNackGenerator::DropPendingUpTo(uint16_t max_seq)
{
	ov::LockGuard<ov::Mutex> lock(_lock);
	if (_initialized == false)
	{
		return;
	}
	auto now = std::chrono::steady_clock::now();
	auto ext_max_opt = ExtendSeq(max_seq);
	if (ext_max_opt.has_value() == false)
	{
		return;
	}
	uint32_t ext_max = *ext_max_opt;
	size_t dropped = 0;
	uint16_t first_dropped_seq = 0;
	uint16_t last_dropped_seq = 0;
	uint32_t min_retry = std::numeric_limits<uint32_t>::max();
	uint32_t max_retry = 0;
	uint32_t total_retry = 0;
	int64_t min_age_ms = 0;
	int64_t max_age_ms = 0;
	auto it = _pending.begin();
	while (it != _pending.end() && it->first <= ext_max)
	{
		auto seq16 = static_cast<uint16_t>(it->first & 0xFFFF);
		auto rc = it->second.retry_count;
		auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.inserted_at).count();
		if (dropped == 0)
		{
			first_dropped_seq = seq16;
			min_age_ms = age;
			max_age_ms = age;
		}
		last_dropped_seq = seq16;
		if (rc < min_retry) min_retry = rc;
		if (rc > max_retry) max_retry = rc;
		total_retry += rc;
		if (age < min_age_ms) min_age_ms = age;
		if (age > max_age_ms) max_age_ms = age;
		_lost_permanent_total++;
		dropped++;
		it = _pending.erase(it);
	}
	// One line per jitter-buffer-driven drop so recovery rate and NACK->RTX
	// timing can be traced against the buffer's processed-seq advances.
	if (dropped > 0)
	{
		double avg_retry = static_cast<double>(total_retry) / static_cast<double>(dropped);
		logtd("DropPendingUpTo track(%u) ssrc(%u) up_to_seq(%u) dropped(%zu) range[%u..%u] "
			  "retries[min(%u) max(%u) avg(%.1f)] age_ms[min(%ld) max(%ld)] pending_remain(%zu) "
			  "ewma(%.1f) dev(%.1f) hold(%u)",
			  _track_id, _media_ssrc, max_seq, dropped, first_dropped_seq, last_dropped_seq,
			  min_retry, max_retry, avg_retry, min_age_ms, max_age_ms, _pending.size(),
			  _ewma_ms, _ewma_dev_ms, GetRecommendedHoldMsInternal());
	}
}

void RtpNackGenerator::UpdateLatencyStats(double sample_ms, std::chrono::steady_clock::time_point now)
{
	// A latency that rounds down to 0ms would drive _ewma_ms to 0 and make the
	// retry interval 0, retrying on every flush. Floor the sample at 1ms.
	if (sample_ms < 1.0)
	{
		sample_ms = 1.0;
	}

	if (_stats_initialized == false)
	{
		_ewma_ms = sample_ms;
		_ewma_dev_ms = sample_ms / 2.0;
		_stats_initialized = true;
	}
	else
	{
		double err = sample_ms - _ewma_ms;
		_ewma_ms += EWMA_ALPHA * err;
		_ewma_dev_ms += EWMA_DEV_ALPHA * (std::fabs(err) - _ewma_dev_ms);
	}

	_last_sample_at = now;
}

void RtpNackGenerator::DiscardStale(std::chrono::steady_clock::time_point now)
{
	auto max_age = std::chrono::milliseconds(MAX_AGE_MS);

	for (auto it = _pending.begin(); it != _pending.end();)
	{
		// Absolute safety cap; the normal end signal is DropPendingUpTo
		// driven by the jitter buffer.
		if ((now - it->second.inserted_at) > max_age)
		{
			auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							  now - it->second.inserted_at)
							  .count();
			logtd("Drop unrecoverable track(%u) ssrc(%u) seq(%u) age(%ldms) retries(%u) reason(age)",
				  _track_id, _media_ssrc, static_cast<uint16_t>(it->first & 0xFFFF),
				  age_ms, it->second.retry_count);
			_lost_permanent_total++;
			it = _pending.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void RtpNackGenerator::LogPeriodicStats(std::chrono::steady_clock::time_point now)
{
	if (now - _last_stats_log_at < std::chrono::milliseconds(STATS_LOG_INTERVAL_MS))
	{
		return;
	}

	uint64_t d_received	 = _received_total - _prev_received;
	uint64_t d_nacks	 = _nacks_sent_total - _prev_nacks_sent;
	uint64_t d_recovered = _recovered_total - _prev_recovered;
	uint64_t d_lost		 = _lost_permanent_total - _prev_lost_permanent;

	double loss_pct = 0.0;
	if (d_received + d_lost > 0)
	{
		loss_pct = 100.0 * static_cast<double>(d_lost) / static_cast<double>(d_received + d_lost);
	}
	double recovery_pct = 0.0;
	if (d_recovered + d_lost > 0)
	{
		recovery_pct = 100.0 * static_cast<double>(d_recovered) / static_cast<double>(d_recovered + d_lost);
	}

	logtd("NackStats track(%u) ssrc(%u) recv(+%lu) nack(+%lu) recovered(+%lu) lost(+%lu) loss(%.2f%%) recovery(%.2f%%) "
		  "totals[recv(%lu) nack(%lu) recovered(%lu) lost(%lu)] pending(%zu) hold(%ums)",
		  _track_id, _media_ssrc, d_received, d_nacks, d_recovered, d_lost, loss_pct, recovery_pct,
		  _received_total, _nacks_sent_total, _recovered_total, _lost_permanent_total,
		  _pending.size(), GetRecommendedHoldMsInternal());

	_prev_received		  = _received_total;
	_prev_nacks_sent	  = _nacks_sent_total;
	_prev_recovered		  = _recovered_total;
	_prev_lost_permanent  = _lost_permanent_total;
	_last_stats_log_at	  = now;
}
