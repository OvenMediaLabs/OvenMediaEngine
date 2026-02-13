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

		PullingItem(const info::VHostAppName &vhost_app_name, const ov::String &stream_name, const std::vector<ov::String> &url_list, off_t offset)
			: _vhost_app_name(vhost_app_name),
			_stream_name(stream_name),
			_url_list(url_list),
			_offset(offset)
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
		info::VHostAppName		_vhost_app_name;
		ov::String				_stream_name;
		std::vector<ov::String> _url_list;
		off_t 					_offset;
		PullingItemState		_state = PullingItemState::PULLING;
		std::shared_mutex 		_mutex;
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

		bool LockPullStreamIfNeeded(const info::Application &app_info, const ov::String &stream_name, const std::vector<ov::String> &url_list, off_t offset);
		bool UnlockPullStreamIfNeeded(const info::Application &app_info, const ov::String &stream_name, PullingItem::PullingItemState state);

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