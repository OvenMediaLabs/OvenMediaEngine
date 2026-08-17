//==============================================================================
//
//  PullProvider Base Class
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/provider/provider.h>
#include <base/mediarouter/mediarouter_interface.h>
#include <orchestrator/interfaces.h>
#include <shared_mutex>

namespace pvd
{
	class PullingItem
	{
	public:
		enum class PullingItemState : uint8_t
		{
			PULLING,
			PULLED,
			ERROR
		};

		PullingItem(const ov::String &key)
			: _key(key)
		{
		}

		void SetState(PullingItemState state)
		{
			_state = state;
		}

		PullingItemState State()
		{
			return _state;
		}

		void Wait()
		{
			std::shared_lock lock(_mutex);
		}

		void Lock()
		{
			_mutex.lock();
		}

		void Unlock()
		{
			_mutex.unlock();
		}

	private:
		ov::String _key;
		std::atomic<PullingItemState> _state = PullingItemState::PULLING;
		std::shared_mutex _mutex;
	};

	class PullApplication;

	// RTMP Server와 같은 모든 Provider는 다음 Interface를 구현하여 MediaRouterInterface에 자신을 등록한다.
	class PullProvider : public Provider, public ocst::PullProviderModuleInterface
	{
	public:
		// Implementation ModuleInterface
		ocst::ModuleType GetModuleType() const override
		{
			return ocst::ModuleType::PullProvider;
		}

	protected:
		PullProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
		virtual ~PullProvider() override;

		std::shared_ptr<PullingItem> LockPullStreamIfNeeded(const ov::String &key);
		bool UnlockPullStreamIfNeeded(const ov::String &key, const std::shared_ptr<PullingItem> &item, PullingItem::PullingItemState state);

		//--------------------------------------------------------------------
		// Implementation of PullProviderModuleInterface
		//--------------------------------------------------------------------
		std::shared_ptr<pvd::Stream> PullStream(
			const std::shared_ptr<const ov::Url> &request_from,
			const info::Application &app_info, const ov::String &stream_name,
			const std::vector<ov::String> &url_list, off_t offset, const std::shared_ptr<pvd::PullStreamProperties> &properties) override;

		bool StopStream(const info::Application &app_info, const std::shared_ptr<pvd::Stream> &stream) override;
		//--------------------------------------------------------------------

	private:	
		ov::String		GeneratePullingKey(const info::VHostAppName &vhost_app_name, const ov::String &stream_name);

		std::map<ov::String, std::shared_ptr<PullingItem>>	_pulling_table;
		std::mutex 											_pulling_table_mutex;
	};

}  // namespace pvd