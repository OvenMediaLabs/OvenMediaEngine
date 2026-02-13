//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/info/host.h>
#include <base/mediarouter/mediarouter_application_observer.h>
#include <base/ovcrypto/ovcrypto.h>
#include <base/publisher/application.h>
#include <base/publisher/stream.h>

#include <modules/ice/ice_port_manager.h>
#include <modules/physical_port/physical_port.h>

#include <orchestrator/interfaces.h>

#include <modules/access_control/access_controller.h>

#include <chrono>

namespace pub
{
	//====================================================================================================
	// Monitoring Collect Data
	//====================================================================================================
	enum class MonitroingCollectionType
	{
		Stream = 0,
		App,
		Origin,
		Host,
	};

	struct MonitoringCollectionData
	{
		MonitoringCollectionData() = default;

		MonitoringCollectionData(MonitroingCollectionType type_,
								 const ov::String &origin_name_ = "",
								 const ov::String &app_name_ = "",
								 const ov::String &stream_name_ = "")
		{
			type = type_;
			type_string = GetTypeString(type);
			origin_name = origin_name_;
			app_name = app_name_;
			stream_name = stream_name_;
		}

		void Append(const std::shared_ptr<pub::MonitoringCollectionData> &collection)
		{
			edge_connection += collection->edge_connection;
			edge_bitrate += collection->edge_bitrate;
			p2p_connection += collection->p2p_connection;
			p2p_bitrate += collection->p2p_bitrate;
		}

		static ov::String GetTypeString(MonitroingCollectionType type)
		{
			ov::String result;

			if (type == MonitroingCollectionType::Stream)
				result = "stream";
			else if (type == MonitroingCollectionType::App)
				result = "app";
			else if (type == MonitroingCollectionType::Origin)
				result = "org";
			else if (type == MonitroingCollectionType::Host)
				result = "host";

			return result;
		}

		MonitroingCollectionType type = MonitroingCollectionType::Stream;
		ov::String type_string;
		ov::String origin_name;
		ov::String app_name;
		ov::String stream_name;
		uint32_t edge_connection = 0;					   // count
		uint64_t edge_bitrate = 0;						   // bps
		uint32_t p2p_connection = 0;					   // count
		uint64_t p2p_bitrate = 0;						   // bps
		std::chrono::system_clock::time_point check_time;  // (chrono)
	};

	// All publishers such as WebRTC, HLS and MPEG-DASH has to inherit the Publisher class and implement that interfaces
	class Publisher : public ocst::PublisherModuleInterface
	{
	public:
		virtual bool Start();
		virtual bool Stop();

		std::shared_ptr<Application> GetApplicationByName(const info::VHostAppName &vhost_app_name);

		// First GetStream(vhost_app_name, stream_name) and if it fails pull stream by the orchetrator
		// If an url is set, the url is higher priority than OriginMap
		std::shared_ptr<Stream> PullStream(const std::shared_ptr<const ov::Url> &request_from, const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name);
		std::shared_ptr<Stream> GetStream(const info::VHostAppName &vhost_app_name, const ov::String &stream_name);
		template <typename T>
		std::shared_ptr<T> GetStreamAs(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
		{
			return std::static_pointer_cast<T>(GetStream(vhost_app_name, stream_name));
		}

		uint32_t GetApplicationCount();
		std::shared_ptr<Application> GetApplicationById(info::application_id_t application_id);
		std::shared_ptr<Stream> GetStream(info::application_id_t application_id, uint32_t stream_id);
		template <typename T>
		std::shared_ptr<T> GetStreamAs(info::application_id_t application_id, uint32_t stream_id)
		{
			return std::static_pointer_cast<T>(GetStream(application_id, stream_id));
		}

		std::shared_ptr<Session> GetSession(const info::Session::Path &session_path);

		//--------------------------------------------------------------------
		// Implementation of ModuleInterface
		//--------------------------------------------------------------------
		bool OnCreateApplication(const info::Application &app_info) override;
		bool OnDeleteApplication(const info::Application &app_info) override;

		// Each Publisher should define their type
		virtual PublisherType GetPublisherType() const = 0;
		virtual const char *GetPublisherName() const = 0;

	protected:
		explicit Publisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
		virtual ~Publisher();

		const cfg::Server &GetServerConfig() const;

		
		virtual std::shared_ptr<Application> OnCreatePublisherApplication(const info::Application &application_info) = 0;
		virtual bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) = 0;

		bool IsAccessControlEnabled(const std::shared_ptr<const ov::Url> &request_url);

		// SignedPolicy is an official feature
		std::tuple<AccessController::VerificationResult, std::shared_ptr<const SignedPolicy>> VerifyBySignedPolicy(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info);
		std::tuple<AccessController::VerificationResult, std::shared_ptr<const SignedPolicy>> VerifyBySignedPolicy(const std::shared_ptr<const ac::RequestInfo> &request_info);
		// AdmissionWebhooks is an official feature
		std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> SendCloseAdmissionWebhooks(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info);
		std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> SendCloseAdmissionWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info);
		std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> VerifyByAdmissionWebhooks(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info);
		std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> VerifyByAdmissionWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info);

		std::map<info::application_id_t, std::shared_ptr<Application>> 	_applications;
		std::shared_mutex 		_application_map_mutex;

		const cfg::Server _server_config;
		std::shared_ptr<MediaRouterInterface> _router;

	private:
		std::shared_ptr<AccessController> _access_controller = nullptr;
	};
}  // namespace pub
