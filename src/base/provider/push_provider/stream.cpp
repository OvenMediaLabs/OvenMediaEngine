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
		// How `PushStream::_activity_state` is laid out.
		// Handlers and the change counter share the word with the reaping flag,
		// so one compare-and-swap can require that none of them has moved.
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

	// Records that something a silence judgment reads has changed, without disturbing the handler count
	// or letting the counter carry into the reaping flag.
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
			if (_activity_state.compare_exchange_weak(state, state + 1))
			{
				return true;
			}
		}

		// A judgment has committed to deleting this channel, so there is nothing left to hand data to.
		return false;
	}

	void PushStream::EndProcessingData()
	{
		// A reservation is only taken while no handler is inside,
		// so this never has to preserve the flag.
		_activity_state.fetch_sub(1);
	}

	bool PushStream::IsProcessingData()
	{
		return (_activity_state.load() & HANDLER_COUNT_MASK) != 0;
	}

	void PushStream::UpdateLastReceivedTime()
	{
		_last_received_time_ms = SteadyNowMs();
		CountStateChange();
	}

	void PushStream::SetPacketSilenceTimeoutMs(time_t timeout_ms)
	{
		_packet_silence_timeout_ms = timeout_ms;
		CountStateChange();
	}

	PushStream::SilenceState PushStream::GetSilenceState()
	{
		SilenceState state;

		// The activity word is read first, so a write to any field below shows up as a change.
		state.activity		= _activity_state.load();
		state.is_processing = (state.activity & HANDLER_COUNT_MASK) != 0;
		state.timeout_ms	= GetPacketSilenceTimeoutMs();
		state.elapsed_ms	= GetElapsedMsSinceLastReceived();

		return state;
	}

	bool PushStream::TryBeginReaping(const SilenceState &state)
	{
		// The word has to still hold no handler, no reservation, and the change count this judgment saw.
		// A single swap therefore rules out a handler being inside,
		// another judgment having reserved the channel,
		// and anything having moved since the state was read.
		// Losing changes nothing, so a judgment that turns out to be stale never costs a packet.
		uint64_t expected = state.activity & CHANGE_COUNT_MASK;

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

		// Apply `PacketSilenceTimeoutMs` only when the operator set it to a positive value.
		// A provider default filled in during config parsing (MPEG-TS gets `1500` ms) is not the
		// operator's intent and keeps applying only once the stream is published, and an explicit `0`
		// leaves the channel-creation default in place rather than removing the guard, which for
		// MPEG-TS over UDP is the only thing that can reap a channel that never publishes.
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

		// A `0` is not honored: it would leave this wait with no timeout at all,
		// and the config layer also yields `0` for an empty or non-numeric element,
		// so a typo must not remove the guard.
		if (is_configured && (configured_ms > 0))
		{
			// The operator sized this wait, so it governs even when `PacketSilenceTimeoutMs` is set too.
			// That option describes a stream that has already published.
			SetPacketSilenceTimeoutMs(configured_ms);
		}
		else
		{
			// This wait was not sized. An operator-configured `PacketSilenceTimeoutMs` keeps governing it,
			// as it did before this option existed; otherwise this option's default applies.
			bool is_silence_configured = false;
			const auto silence_ms	   = application->GetConfiguredPacketSilenceTimeoutMs(provider_type, &is_silence_configured);

			SetPacketSilenceTimeoutMs((is_silence_configured && (silence_ms > 0))
										  ? silence_ms
										  : cfg::vhost::app::pvd::DEFAULT_FIRST_MEDIA_WAIT_TIMEOUT_MS);
		}

		// The budget is in place, so the first media packet can end this wait from here.
		// Arming it only now is what keeps a media message that arrived earlier from consuming it.
		_first_media_wait_phase = FirstMediaWaitPhase::Waiting;
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
			// The wait keeps running so a later packet can still end it,
			// rather than being spent on a lookup that may succeed next time.
			return;
		}

		bool is_configured		 = false;
		const auto configured_ms = application->GetConfiguredPacketSilenceTimeoutMs(provider->GetProviderType(), &is_configured);

		auto expected = FirstMediaWaitPhase::Waiting;
		if (_first_media_wait_phase.compare_exchange_strong(expected, FirstMediaWaitPhase::Ended) == false)
		{
			// Another packet ended the wait first.
			return;
		}

		// Only a value the operator set positively governs the channel from here.
		// Everything else falls back to the budget an unpublished channel is created with.
		// This wait's budget is sized for a source that has not sent anything yet,
		// so keeping it would let a channel that sent one packet and stopped hold on for that long again.
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
