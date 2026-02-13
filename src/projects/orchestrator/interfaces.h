//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/info.h>

#include "enums.h"

// Forward declaration
namespace pvd
{
	class Stream;
	class PullStreamProperties;
}

namespace ocst
{
	//--------------------------------------------------------------------
	// Interfaces
	//--------------------------------------------------------------------
	class ModuleInterface : public ov::EnableSharedFromThis<ModuleInterface>
	{
	public:
		void SetModuleAvailable(bool flag) {_module_available = flag;}
		bool IsModuleAvailable() { return _module_available;}

		virtual ModuleType GetModuleType() const = 0;

		//--------------------------------------------------------------------
		// Event callbacks
		//--------------------------------------------------------------------

		/// Called when the vhost is created
		///
		/// @param app_info The information of the vhost
		virtual bool OnCreateHost(const info::Host &host_info) = 0;

		/// Called when the vhost is deleted
		///
		/// @param app_info The information of the vhost
		virtual bool OnDeleteHost(const info::Host &host_info) = 0;

		// Called when the certificate is updated
		virtual bool OnUpdateCertificate(const info::Host &host_info) {return true;}

		/// Called when the application is created
		///
		/// @param app_info The information of the application
		virtual bool OnCreateApplication(const info::Application &app_info) = 0;

		/// Called when the application is deleted
		///
		/// @param app_info The information of the application
		virtual bool OnDeleteApplication(const info::Application &app_info) = 0;

	private:
		bool _module_available = false;
	};

	class PullProviderModuleInterface
	{
	public:
		ModuleType GetModuleType() const
		{
			return ModuleType::PullProvider;
		}

		/// Called when another module is requested to pull stream list
		///
		/// @param request_from Source from which PullStream() invoked (Mainly provided when requested by Publisher)
		/// @param app_info An information of the application
		/// @param stream_name A stream name to create
		/// @param url_list The streaming URLs to pull
		/// @param offset Specifies the starting point of the streaming URL (unit: milliseconds)
		/// @param properties properties if PullStream
		///
		/// @return Newly created stream instance
		virtual std::shared_ptr<pvd::Stream> PullStream(
			const std::shared_ptr<const ov::Url> &request_from,
			const info::Application &app_info, const ov::String &stream_name,
			const std::vector<ov::String> &url_list, off_t offset, 
			const std::shared_ptr<pvd::PullStreamProperties> &properties) = 0;

		virtual bool StopStream(const info::Application &app_info, const std::shared_ptr<pvd::Stream> &stream) = 0;
	};

	class MediaRouterModuleInterface : public ModuleInterface
	{
	public:
		ModuleType GetModuleType() const override
		{
			return ModuleType::MediaRouter;
		}
	};

	class TranscoderModuleInterface : public ModuleInterface
	{
	public:
		ModuleType GetModuleType() const override
		{
			return ModuleType::Transcoder;
		}
	};

	class PublisherModuleInterface : public ModuleInterface
	{
	public:
		ModuleType GetModuleType() const override
		{
			return ModuleType::Publisher;
		}
	};
}  // namespace ocst
