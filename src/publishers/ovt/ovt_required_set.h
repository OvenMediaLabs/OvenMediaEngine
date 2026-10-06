//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// The required tokens a stream has learned at packetization time
// (a transport format or packet type an OVT1 peer has no value for).
// The list is replaced copy-on-write and the version grows with every change,
// so a session compares one integer right before delivering media and
// sends the current list first when it differs from the last one it sent,
// or when it never sent one. That single rule covers a new session and a later change alike,
// and it puts PT 40 ahead of the first packet carrying the value.
class OvtRequiredSet
{
public:
	struct Snapshot
	{
		uint32_t version = 0;
		std::shared_ptr<const std::vector<ov::String>> tokens;
	};

	// Returns true when the token was new
	bool Add(const ov::String &token)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		if (std::find(_tokens->begin(), _tokens->end(), token) != _tokens->end())
		{
			return false;
		}

		auto tokens = std::make_shared<std::vector<ov::String>>(*_tokens);
		tokens->push_back(token);
		_tokens = tokens;
		_version++;

		return true;
	}

	Snapshot Get() const
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return Snapshot{_version.load(), _tokens};
	}

	// Lock-free read for the per-packet comparison
	uint32_t GetVersion() const
	{
		return _version.load(std::memory_order_acquire);
	}

private:
	mutable std::mutex _mutex;
	std::atomic<uint32_t> _version						   = 1;
	std::shared_ptr<const std::vector<ov::String>> _tokens = std::make_shared<const std::vector<ov::String>>();
};

// One session's memory of the last required set it delivered. Used on the stream worker only.
class OvtRequiredCursor
{
public:
	bool NeedsSend(uint32_t version) const
	{
		return (_sent_version.has_value() == false) || (*_sent_version != version);
	}

	void MarkSent(uint32_t version)
	{
		_sent_version = version;
	}

private:
	std::optional<uint32_t> _sent_version;
};

// One session's memory of the track epoch its describe reflected.
// The send gate discards everything up to the first marker packet,
// so a NOTIFY broadcast in that window never reached the session and
// the tracks it learned at describe may be stale.
// The first delivery after the gate opens settles it: the epoch either matches,
// and nothing is owed, or it does not, and the session is sent every track's configuration once.
// Nothing is compared afterwards, so a connection that attaches and detaches repeatedly pays one atomic read.
// The describe epoch is written once by the socket worker, before `AddSession()` publishes the session;
// everything after that runs on the stream worker.
class OvtTrackEpochCursor
{
public:
	void SetDescribeEpoch(uint32_t epoch)
	{
		_describe_epoch = epoch;
	}

	// No describe of this stream on this connection, so there is no epoch to compare against
	// and a coincidental match must not settle the session. The first delivery sends the snapshot.
	void SetDescribeUnknown()
	{
		_describe_epoch.reset();
		_describe_unknown = true;
	}

	// Whether this session is still owed a snapshot. False once settled.
	bool NeedsSnapshot(uint32_t epoch) const
	{
		return _describe_unknown || (_describe_epoch.has_value() && (*_describe_epoch != epoch));
	}

	// True once `MarkSettled()` has run, which is when the comparison is done with for good
	bool IsSettled() const
	{
		return (_describe_unknown == false) && (_describe_epoch.has_value() == false);
	}

	void MarkSettled()
	{
		_describe_epoch.reset();
		_describe_unknown = false;
	}

private:
	std::optional<uint32_t> _describe_epoch;
	bool _describe_unknown = false;
};
