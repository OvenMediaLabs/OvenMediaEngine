//==============================================================================
//
//  PushStream
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================


#include "provider.h"
#include "application.h"
#include "stream.h"
#include "provider_private.h"

namespace pvd
{
	namespace
	{
		// `PushStream::_activity_state` layout. All three live in one word,
		// so a single compare-and-swap can require that none of them has moved.
		constexpr uint64_t HANDLER_COUNT_MASK = 0x000000000000FFFFULL;
		constexpr uint64_t CHANGE_COUNT_MASK  = 0x7FFFFFFFFFFF0000ULL;
		constexpr uint64_t CHANGE_COUNT_STEP  = 0x0000000000010000ULL;
		constexpr uint64_t REAPING_FLAG		  = 0x8000000000000000ULL;

		int64_t SteadyNowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::steady_clock::now().time_since_epoch())
				.count();
		}
	}  // namespace

	PushStream::PushStream(StreamSourceType source_type, ov::String channel_name, uint32_t channel_id, const std::shared_ptr<PushProvider> &provider)
		: PushStream(source_type, channel_id, provider)
	{
		SetName(channel_name);
	}

	PushStream::PushStream(StreamSourceType source_type, uint32_t channel_id, const std::shared_ptr<PushProvider> &provider)
		: Stream(source_type)
	{
		SetId(pvd::Application::IssueUniqueStreamId());
		_channel_id = channel_id;
		_provider = provider;
	}

	PushStream::PushStream(StreamSourceType source_type, ov::String channel_name, const std::shared_ptr<PushProvider> &provider)
		: PushStream(source_type, provider)
	{
		SetName(channel_name);
	}

	PushStream::PushStream(StreamSourceType source_type, const std::shared_ptr<PushProvider> &provider)
		: Stream(source_type)
	{
		auto id = pvd::Application::IssueUniqueStreamId();
		SetId(id);
		// If the channel id is not given, use the stream id as the channel id
		_channel_id = id;
		_provider = provider;
	}

	bool PushStream::Terminate()
	{
		// To PushStream, Terminate has the same meaning as Stop.
		if (Stop() == false)
		{
			return false;
		}

		return Stream::Terminate();
	}

	uint32_t PushStream::GetChannelId()
	{
		return _channel_id;
	}

	uint32_t PushStream::GetRelatedChannelId()
	{
		return _related_channel_id;
	}

	void PushStream::SetRelatedChannelId(uint32_t related_channel_id)
	{
		_related_channel_id = related_channel_id;
	}

	// Marks a write to something `GetSilenceState()` reads. Called once before and once after the write,
	// so a lone write leaves the counter odd while it is in flight,
	// which is how `TryBeginReaping()` rejects a state read across one.
	// Writes that overlap are covered by the handler count instead.
	void PushStream::CountStateChange()
	{
		auto state = _activity_state.load();

		while (true)
		{
			const uint64_t counted = (state & CHANGE_COUNT_MASK) + CHANGE_COUNT_STEP;
			const uint64_t next	   = (state & ~CHANGE_COUNT_MASK) | (counted & CHANGE_COUNT_MASK);

			if (_activity_state.compare_exchange_weak(state, next))
			{
				return;
			}
		}
	}

	bool PushStream::BeginProcessingData()
	{
		auto state = _activity_state.load();

		while ((state & REAPING_FLAG) == 0)
		{
			const auto handler_count = (state & HANDLER_COUNT_MASK);
			if (handler_count == HANDLER_COUNT_MASK)
			{
				// Prevent carry into the change counter/`REAPING_FLAG` region.
				return false;
			}

			const auto next = (state & ~HANDLER_COUNT_MASK) | (handler_count + 1);
			if (_activity_state.compare_exchange_weak(state, next))
			{
				return true;
			}
		}

		// `TryBeginReaping()` has succeeded on this channel, so there is nothing left to hand data to.
		return false;
	}

	void PushStream::EndProcessingData()
	{
		// `TryBeginReaping()` only succeeds while no `OnDataReceived()` is inside,
		// so this never has to preserve `REAPING_FLAG`.
		_activity_state.fetch_sub(1);
	}

	bool PushStream::IsProcessingData()
	{
		return (_activity_state.load() & HANDLER_COUNT_MASK) != 0;
	}

	void PushStream::UpdateLastReceivedTime()
	{
		CountStateChange();
		_last_received_time_ms = SteadyNowMs();
		CountStateChange();
	}

	void PushStream::SetPacketSilenceTimeoutMs(time_t timeout_ms)
	{
		CountStateChange();
		_packet_silence_timeout_ms = timeout_ms;
		CountStateChange();
	}

	PushStream::SilenceState PushStream::GetSilenceState()
	{
		SilenceState state;

		// `_activity_state` is read first, so a write to any field below shows up as a change.
		state.activity		= _activity_state.load();
		state.is_processing = (state.activity & HANDLER_COUNT_MASK) != 0;
		state.timeout_ms	= GetPacketSilenceTimeoutMs();
		state.elapsed_ms	= GetElapsedMsSinceLastReceived();

		return state;
	}

	bool PushStream::TryBeginReaping(const SilenceState &state)
	{
		uint64_t expected = state.activity & CHANGE_COUNT_MASK;

		if ((expected & CHANGE_COUNT_STEP) != 0)
		{
			// A write was in flight while the fields below the counter were read.
			return false;
		}

		// The word must still hold no `OnDataReceived()`, no `REAPING_FLAG`,
		// and the change count that `state` was read with.
		// Losing the swap changes nothing, so a stale caller costs no data.
		return _activity_state.compare_exchange_strong(expected, expected | REAPING_FLAG);
	}

	time_t PushStream::GetPacketSilenceTimeoutMs()
	{
		return _packet_silence_timeout_ms;
	}

	time_t PushStream::GetElapsedMsSinceLastReceived()
	{
		auto last_received_time_ms = _last_received_time_ms.load();
		if (last_received_time_ms < 0)
		{
			return -1;
		}

		return static_cast<time_t>(SteadyNowMs() - last_received_time_ms);
	}

	void PushStream::ApplyConfiguredPacketSilenceTimeoutMs(const info::VHostAppName &vhost_app_name)
	{
		auto provider = GetProvider();
		if (provider == nullptr)
		{
			return;
		}

		auto application = provider->GetApplicationByName(vhost_app_name);
		if (application == nullptr)
		{
			return;
		}

		// Only a positive value the operator set applies here. A provider default filled in during
		// config parsing (MPEG-TS gets `1500` ms) is not the operator's intent, and an explicit `0`
		// leaves the channel-creation default in place instead of removing the timeout altogether.
		bool is_configured	  = false;
		const auto timeout_ms = application->GetConfiguredPacketSilenceTimeoutMs(provider->GetProviderType(), &is_configured);

		if (is_configured && (timeout_ms > 0))
		{
			SetPacketSilenceTimeoutMs(timeout_ms);
		}
	}

	void PushStream::ApplyConfiguredFirstMediaWaitTimeoutMs(const info::VHostAppName &vhost_app_name)
	{
		auto provider = GetProvider();
		if (provider == nullptr)
		{
			return;
		}

		auto application = provider->GetApplicationByName(vhost_app_name);
		if (application == nullptr)
		{
			return;
		}

		const auto provider_type = provider->GetProviderType();

		bool is_configured		 = false;
		const auto configured_ms = application->GetConfiguredFirstMediaWaitTimeoutMs(provider_type, &is_configured);

		// With the option unset no wait runs, and `PacketSilenceTimeoutMs` governs this window instead.
		// A `0` cannot get here: the config layer rejects it, as it does an empty or non-numeric value.
		if ((is_configured == false) || (configured_ms <= 0))
		{
			ApplyConfiguredPacketSilenceTimeoutMs(vhost_app_name);
			return;
		}

		// This wait governs even with `PacketSilenceTimeoutMs` set, which sizes a published stream.
		SetPacketSilenceTimeoutMs(configured_ms);

		// Armed only after the timeout is in place, so a message that arrived earlier cannot consume it.
		_first_media_wait_phase = FirstMediaWaitPhase::Waiting;
	}

	bool PushStream::IsWaitingForFirstMedia() const
	{
		return _first_media_wait_phase.load() == FirstMediaWaitPhase::Waiting;
	}

	void PushStream::EndFirstMediaWait(const info::VHostAppName &vhost_app_name)
	{
		if (_first_media_wait_phase.load() != FirstMediaWaitPhase::Waiting)
		{
			// Either the provider has not sized the wait yet, or the wait is already over.
			return;
		}

		auto provider = GetProvider();
		if (provider == nullptr)
		{
			return;
		}

		auto application = provider->GetApplicationByName(vhost_app_name);
		if (application == nullptr)
		{
			// The wait keeps running, so a later message can still end it.
			return;
		}

		bool is_configured		 = false;
		const auto configured_ms = application->GetConfiguredPacketSilenceTimeoutMs(provider->GetProviderType(), &is_configured);

		auto expected = FirstMediaWaitPhase::Waiting;
		if (_first_media_wait_phase.compare_exchange_strong(expected, FirstMediaWaitPhase::Ended) == false)
		{
			// Another message ended the wait first.
			return;
		}

		// Only a positive value the operator set governs from here,
		// and everything else falls back to the channel-creation timeout.
		// This wait is sized for a source that has not sent anything yet,
		// so it must not survive the first frame.
		SetPacketSilenceTimeoutMs((is_configured && (configured_ms > 0))
									  ? configured_ms
									  : DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);
	}

	bool PushStream::PublishChannel(const info::VHostAppName &vhost_app_name)
	{
		if(GetProvider() == nullptr)
		{
			return false;
		}

		_attemps_publish_count++;
		
		_is_published = GetProvider()->PublishChannel(GetChannelId(), vhost_app_name, GetSharedPtrAs<PushStream>());

		UpdateLastReceivedTime();

		return _is_published;
	}

	bool PushStream::DoesBelongApplication()
	{
		return GetApplication() != nullptr;
	}

	bool PushStream::IsPublished()
	{
		return _is_published;
	}

	bool PushStream::IsReadyToReceiveStreamData()
	{
		// Check if it is signalling channel
		if(GetPushStreamType() == PushStreamType::SIGNALLING || GetPushStreamType() == PushStreamType::UNKNOWN)
		{
			return false;
		}

		// Check if it has stream name
		if(GetName().GetLength() == 0)
		{
			return false;
		}

		// Check if it has track information
		if(GetTracks().size() == 0)
		{
			return false;
		}

		return true;
	}
}  // namespace pvd
