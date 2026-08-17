//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "mpegts_datagram_reorder_buffer.h"

#include <base/ovlibrary/clock.h>

#include "mpegts_packet.h"

namespace mpegts
{
	DatagramReorderBuffer::DatagramReorderBuffer(size_t max_datagrams, int64_t timeout_msec, std::function<int64_t()> clock)
		: _max_datagrams(max_datagrams < 1 ? 1 : max_datagrams),
		  _timeout_msec(timeout_msec),
		  _clock(std::move(clock))
	{
	}

	int64_t DatagramReorderBuffer::Now() const
	{
		return _clock ? _clock() : static_cast<int64_t>(ov::Clock::NowMSec());
	}

	uint8_t DatagramReorderBuffer::Displacement(uint8_t cc, uint8_t expected)
	{
		return static_cast<uint8_t>((cc - expected) & 0x0F);
	}

	DatagramReorderBuffer::Summary DatagramReorderBuffer::Parse(const std::shared_ptr<const ov::Data> &datagram)
	{
		Summary summary;

		if (datagram == nullptr)
		{
			return summary;
		}

		const uint8_t *data = datagram->GetDataAs<uint8_t>();
		const size_t length = datagram->GetLength();

		// A UDP datagram must be a whole number of TS packets to be trusted for CC-based ordering.
		if ((data == nullptr) || (length < MPEGTS_MIN_PACKET_SIZE) || ((length % MPEGTS_MIN_PACKET_SIZE) != 0))
		{
			return summary;
		}

		for (size_t offset = 0; offset < length; offset += MPEGTS_MIN_PACKET_SIZE)
		{
			if (data[offset] != MPEGTS_SYNC_BYTE)
			{
				// Not aligned; the summary is left un-aligned so the caller bypasses reordering.
				return summary;
			}

			const bool transport_error			   = (data[offset + 1] & 0x80) != 0;
			const uint16_t pid					   = static_cast<uint16_t>(((data[offset + 1] & 0x1F) << 8) | data[offset + 2]);
			const uint8_t adaptation_field_control = (data[offset + 3] >> 4) & 0x03;
			const uint8_t continuity_counter	   = data[offset + 3] & 0x0F;
			const bool has_adaptation			   = (adaptation_field_control & 0b10) != 0;
			const bool has_payload				   = (adaptation_field_control & 0b01) != 0;

			if (has_adaptation)
			{
				const uint8_t adaptation_field_length	  = data[offset + 4];

				// A valid adaptation field cannot exceed the packet body: at most 183 bytes when
				// adaptation-only (AFC=10), and 182 when a payload must also follow (AFC=11).
				// A larger value means the packet is corrupt; treat the whole datagram as untrusted
				// so it bypasses reordering rather than being sequenced on possibly-bogus counters.
				const uint8_t max_adaptation_field_length = has_payload
																? (MPEGTS_MIN_PACKET_SIZE - MPEGTS_HEADER_SIZE - 2)
																: (MPEGTS_MIN_PACKET_SIZE - MPEGTS_HEADER_SIZE - 1);
				if (adaptation_field_length > max_adaptation_field_length)
				{
					return summary;
				}

				if ((adaptation_field_length > 0) && ((data[offset + 5] & 0x80) != 0))
				{
					summary.discontinuity = true;
				}
			}

			// Only payload-bearing packets advance the continuity counter.
			// Transport-error and null packets carry no usable continuity information.
			if (transport_error || (has_payload == false) || (pid == 0x1FFF))
			{
				continue;
			}

			if (summary.first_cc.find(pid) == summary.first_cc.end())
			{
				summary.first_cc[pid] = continuity_counter;
				summary.pid_order.push_back(pid);
			}
			summary.last_cc[pid] = continuity_counter;
		}

		summary.aligned = true;
		return summary;
	}

	DatagramReorderBuffer::Placement DatagramReorderBuffer::Classify(const Summary &summary) const
	{
		bool has_tracked = false;
		bool all_exact	 = true;
		bool any_behind	 = false;

		for (uint16_t pid : summary.pid_order)
		{
			const auto it = _expected_cc.find(pid);
			if (it == _expected_cc.end())
			{
				// A PID we have never seen carries no ordering constraint on its own.
				continue;
			}

			has_tracked				   = true;
			const uint8_t displacement = Displacement(summary.first_cc.at(pid), it->second);
			if (displacement != 0)
			{
				all_exact = false;
			}
			if (displacement > AHEAD_LIMIT)
			{
				any_behind = true;
			}
		}

		if (has_tracked == false)
		{
			return Placement::Init;
		}
		if (any_behind)
		{
			// At least one PID is behind expected: this datagram overlaps already-delivered data
			// (a stale or duplicate datagram), so it is not a future packet.
			return Placement::Stale;
		}
		if (all_exact)
		{
			return Placement::InOrder;
		}
		return Placement::Ahead;
	}

	void DatagramReorderBuffer::Deliver(const std::shared_ptr<const ov::Data> &data, const Summary &summary,
										std::vector<std::shared_ptr<const ov::Data>> *out)
	{
		out->push_back(data);
		for (uint16_t pid : summary.pid_order)
		{
			_expected_cc[pid] = static_cast<uint8_t>((summary.last_cc.at(pid) + 1) & 0x0F);
		}
	}

	void DatagramReorderBuffer::Cascade(std::vector<std::shared_ptr<const ov::Data>> *out)
	{
		bool progress = true;
		while (progress)
		{
			progress = false;

			for (auto it = _buffer.begin(); it != _buffer.end();)
			{
				const Placement placement = Classify(it->summary);
				if (placement == Placement::InOrder)
				{
					Deliver(it->data, it->summary, out);
					it		 = _buffer.erase(it);
					progress = true;
				}
				else if (placement == Placement::Stale)
				{
					// Expected advanced past it. It is NOT necessarily a duplicate:
					// with multi-packet datagrams the 4-bit counter can make a still-needed datagram look behind after
					// a flush picked a different victim.
					// Never erase it (that would silently drop a received datagram); pass it through out of order
					// without advancing the baseline and let the downstream continuity check handle any residual disorder.
					out->push_back(it->data);
					it		 = _buffer.erase(it);
					progress = true;
				}
				else
				{
					++it;
				}
			}
		}
	}

	std::deque<DatagramReorderBuffer::Pending>::iterator DatagramReorderBuffer::SelectClosest()
	{
		// Rank each buffered datagram by its smallest per-PID displacement.
		// Across datagrams with disjoint PID sets this compares different counters,
		// so it is a best-effort pick, not a total order - but it only runs on the flush (loss) path,
		// and Cascade never drops the losers (it passes them through),
		// so at worst it adds some out-of-order at a loss boundary.
		auto best		  = _buffer.begin();

		uint8_t best_rank = 0xFF;
		for (auto it = _buffer.begin(); it != _buffer.end(); ++it)
		{
			uint8_t rank = 0xFF;
			for (uint16_t pid : it->summary.pid_order)
			{
				const auto expected = _expected_cc.find(pid);
				if (expected == _expected_cc.end())
				{
					continue;
				}

				const uint8_t displacement = Displacement(it->summary.first_cc.at(pid), expected->second);
				if (displacement < rank)
				{
					rank = displacement;
				}
			}

			if (rank < best_rank)
			{
				best_rank = rank;
				best	  = it;
			}
		}
		return best;
	}

	size_t DatagramReorderBuffer::FlushIfNeeded(std::vector<std::shared_ptr<const ov::Data>> *out)
	{
		const int64_t now	 = Now();

		// The head gap is presumed lost once the buffer is full (depth) or the oldest arrival has
		// waited too long (timeout, evaluated on each arrival - there is no timer thread).
		// Force the datagram nearest the expected counters out (declaring the gap before it lost) and cascade.
		size_t declared_lost = 0;

		while ((_buffer.empty() == false) &&
			   ((_buffer.size() >= _max_datagrams) ||
				((now - _buffer.front().arrival_msec) >= _timeout_msec)))
		{
			auto victim	   = SelectClosest();

			Pending picked = *victim;

			_buffer.erase(victim);

			Deliver(picked.data, picked.summary, out);
			Cascade(out);

			declared_lost++;
		}
		return declared_lost;
	}

	void DatagramReorderBuffer::DrainInOrder(std::vector<std::shared_ptr<const ov::Data>> *out)
	{
		Cascade(out);

		while (_buffer.empty() == false)
		{
			auto victim	   = SelectClosest();
			Pending picked = *victim;
			_buffer.erase(victim);
			Deliver(picked.data, picked.summary, out);
			Cascade(out);
		}
	}

	void DatagramReorderBuffer::Rebase(const Summary &summary)
	{
		for (uint16_t pid : summary.pid_order)
		{
			_expected_cc[pid] = static_cast<uint8_t>((summary.last_cc.at(pid) + 1) & 0x0F);
		}
	}

	size_t DatagramReorderBuffer::Enqueue(const std::shared_ptr<const ov::Data> &datagram,
										  std::vector<std::shared_ptr<const ov::Data>> *out)
	{
		const Summary summary = Parse(datagram);

		if ((summary.aligned == false) || summary.discontinuity)
		{
			// Misaligned or a signalled discontinuity: ordering cannot be trusted.
			// Drain what we have in restored order and pass this datagram through.
			DrainInOrder(out);
			out->push_back(datagram);

			if (summary.pid_order.empty())
			{
				// Untrusted with no usable PID (e.g. misaligned): drop all baselines
				// so they are re-established on the next clean datagram.
				_expected_cc.clear();
			}
			else
			{
				// A signalled discontinuity that still carries PIDs: continuity resumes from it.
				Rebase(summary);
			}
			return 0;
		}

		if (summary.pid_order.empty())
		{
			// Aligned but carries no payload PID (e.g. a PCR-only or null-padding datagram):
			// it holds no continuity information, so pass it straight through WITHOUT disturbing sequencing state.
			out->push_back(datagram);
			return FlushIfNeeded(out);
		}

		switch (Classify(summary))
		{
			case Placement::Init:
			case Placement::InOrder:
				// First datagram / all tracked PIDs continue exactly: deliver and cascade.
				Deliver(datagram, summary, out);
				Cascade(out);
				break;

			case Placement::Ahead:
				// A gap on at least one PID with none behind: buffer until the gap fills.
				// A duplicate of an already-buffered datagram is intentionally NOT deduped here:
				// with multi-packet datagrams a counter match is not proof of duplication
				// (a genuinely different datagram aliases to the same counter every 16 packets),
				// so dropping on a match could lose real data.
				// If a real duplicate is buffered, Cascade's Stale path passes it through out of
				// order (never drops), and the downstream continuity check absorbs it.
				_buffer.push_back({datagram, summary, Now()});
				break;

			case Placement::Stale:
				// Overlaps already-delivered data (stale/duplicate, or a high-rate PID aliasing a deep reorder).
				// Never buffer or drop it: pass it through and let the downstream continuity check handle it.
				// Do not advance the baseline.
				out->push_back(datagram);
				break;
		}

		return FlushIfNeeded(out);
	}

	void DatagramReorderBuffer::Flush(std::vector<std::shared_ptr<const ov::Data>> *out)
	{
		// Teardown drain: emit everything still buffered in restored (oldest-first) order.
		DrainInOrder(out);
	}
}  // namespace mpegts
