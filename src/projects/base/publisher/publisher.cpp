#include "publisher.h"
#include "publisher_private.h"
#include <orchestrator/orchestrator.h>
#include <base/provider/pull_provider/stream_props.h>

namespace pub
{
	Publisher::Publisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: _server_config(server_config), _router(router)
	{
	}

	Publisher::~Publisher()
	{
	}

	bool Publisher::Start()
	{
		logti("%s has been started.", GetPublisherName());

		SetModuleAvailable(true);

		_access_controller = std::make_shared<AccessController>(GetPublisherType(), GetServerConfig());

		return true;
	}

	bool Publisher::Stop()
	{
		std::unique_lock<std::shared_mutex> lock(_application_map_mutex);
		auto it = _applications.begin();

		while (it != _applications.end())
		{
			auto application = it->second;

			_router->UnregisterObserverApp(*application.get(), application);

			it = _applications.erase(it);
		}

		logti("%s has been stopped.", GetPublisherName());
		SetModuleAvailable(false);
		return true;
	}

	const cfg::Server &Publisher::GetServerConfig() const
	{
		return _server_config;
	}

	// Create Application
	bool Publisher::OnCreateApplication(const info::Application &app_info)
	{
		// Check configuration
		auto cfg_publisher_list = app_info.GetConfig().GetPublishers().GetPublisherList();
		for(const auto &cfg_publisher : cfg_publisher_list)
		{
			if(cfg_publisher->GetType() == GetPublisherType())
			{
				if(cfg_publisher->IsParsed())
				{
					break;
				}
				else
				{
					// This provider is diabled
					logtw("%s publisher is disabled in %s application, so it was not created", 
							::StringFromPublisherType(GetPublisherType()).CStr(), app_info.GetVHostAppName().CStr());
					return true;
				}
			}
		}

		auto application = OnCreatePublisherApplication(app_info);
		if (application == nullptr)
		{
			// It may not be a error that the Application failed due to disabling that Publisher.
			// Failure to create a single application should not affect the whole.
			// TODO(Getroot): The reason for the error must be defined and handled in detail.
			return true;
		}

		// 생성한 Application을 Router와 연결하고 Start
		if (_router->RegisterObserverApp(*application.get(), application) == false)
		{
			logte("Failed to register application(%s/%s) to router", GetPublisherName(), app_info.GetVHostAppName().CStr());
			return false;
		}
		

		// Application Map에 보관
		std::lock_guard<std::shared_mutex> lock(_application_map_mutex);
		_applications[application->GetId()] = application;

		return true;
	}

	// Delete Application
	bool Publisher::OnDeleteApplication(const info::Application &app_info)
	{
		std::unique_lock<std::shared_mutex> lock(_application_map_mutex);
		auto item = _applications.find(app_info.GetId());

		logtt("Delete the application: [%s]", app_info.GetVHostAppName().CStr());
		if(item == _applications.end())
		{
			// Check the reason the app is not created is because it is disabled in the configuration
			if(app_info.IsDynamicApp() == false)
			{
				auto cfg_publisher_list = app_info.GetConfig().GetPublishers().GetPublisherList();
				for(const auto &cfg_publisher : cfg_publisher_list)
				{
					if(cfg_publisher->GetType() == GetPublisherType())
					{
						// this provider is disabled
						if(!cfg_publisher->IsParsed())
						{
							return true;
						}
					}
				}
			}

			logte("%s publisher hasn't the %s application.", ::StringFromPublisherType(GetPublisherType()).CStr(), app_info.GetVHostAppName().CStr());
			return false;
		}

		auto application = item->second;
		application->Stop();
		_applications.erase(item);

		lock.unlock();

		_router->UnregisterObserverApp(*application.get(), application);
		
		bool result = OnDeletePublisherApplication(application);
		if(result == false)
		{
			logte("Could not delete the %s application of the %s publisher", app_info.GetVHostAppName().CStr(), ::StringFromPublisherType(GetPublisherType()).CStr());
			return false;
		}

		return true;
	}

	uint32_t Publisher::GetApplicationCount()
	{
		return _applications.size();
	}

	std::shared_ptr<Application> Publisher::GetApplicationByName(const info::VHostAppName &vhost_app_name)
	{
		std::shared_lock<std::shared_mutex> lock(_application_map_mutex);
		for (auto const &x : _applications)
		{
			auto application = x.second;
			if (application->GetVHostAppName() == vhost_app_name)
			{
				return application;
			}
		}

		return nullptr;
	}

	std::shared_ptr<Stream> Publisher::PullStream(const std::shared_ptr<const ov::Url> &request_from, const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name)
	{
		auto stream = GetStream(vhost_app_name, stream_name);
		if(stream != nullptr)
		{
			return stream;
		}

		auto orchestrator = ocst::Orchestrator::GetInstance();
		auto &vapp_name = vhost_app_name.ToString();

		// Pull stream with the local origin map
		logti("Try to pull stream from local origin map: [%s/%s]", vapp_name.CStr(), stream_name.CStr());
		if (orchestrator->RequestPullStreamWithOriginMap(request_from, vhost_app_name, stream_name) == false)
		{
			// Pull stream with the origin map store
			logti("Try to pull stream from origin map store: [%s/%s]", vapp_name.CStr(), stream_name.CStr());
			auto origin_url = orchestrator->GetOriginUrlFromOriginMapStore(vhost_app_name, stream_name);
			if (origin_url == nullptr)
			{
				return nullptr;
			}

			auto properties = std::make_shared<pvd::PullStreamProperties>();
			properties->EnableFromOriginMapStore(true);
			if (origin_url->Scheme().UpperCaseString() == "OVT")
			{
				properties->EnableRelay(true);
			}

			if (orchestrator->RequestPullStreamWithUrls(request_from, vhost_app_name, stream_name, {origin_url->ToUrlString()}, 0, properties) == false)
			{
				return nullptr;
			}
		}

		// try one more after pulling stream
		return GetStream(vhost_app_name, stream_name);
	}

	std::shared_ptr<Stream> Publisher::GetStream(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
	{
		auto app = GetApplicationByName(vhost_app_name);
		if (app != nullptr)
		{
			return app->GetStream(stream_name);
		}

		return nullptr;
	}

	std::shared_ptr<Application> Publisher::GetApplicationById(info::application_id_t application_id)
	{
		std::shared_lock<std::shared_mutex> lock(_application_map_mutex);

		auto application = _applications.find(application_id);
		if (application == _applications.end())
		{
			return nullptr;
		}

		return application->second;
	}

	std::shared_ptr<Stream> Publisher::GetStream(info::application_id_t application_id, uint32_t stream_id)
	{
		auto app = GetApplicationById(application_id);
		if (app != nullptr)
		{
			return app->GetStream(stream_id);
		}

		return nullptr;
	}

	bool Publisher::IsAccessControlEnabled(const std::shared_ptr<const ov::Url> &request_url)
	{
		auto orchestrator = ocst::Orchestrator::GetInstance();
		auto vhost_name = orchestrator->GetVhostNameFromDomain(request_url->Host());

		if (vhost_name.IsEmpty())
		{
			return false;
		}

		auto item = ocst::Orchestrator::GetInstance()->GetHostInfo(vhost_name);
		if (item.has_value())
		{
			auto vhost_item = item.value();

			auto &webhooks_config = vhost_item.GetAdmissionWebhooks();
			auto &signed_policy_config = vhost_item.GetSignedPolicy();
			if (webhooks_config.IsParsed() == true)
			{
				if (webhooks_config.IsEnabledPublisher(GetPublisherType()) == true)
				{
					return true;
				}
			}

			if (signed_policy_config.IsParsed() == true)
			{
				if (signed_policy_config.IsEnabledPublisher(GetPublisherType()) == true)
				{
					return true;
				}
			}
		}

		return false;
	}

	std::tuple<AccessController::VerificationResult, std::shared_ptr<const SignedPolicy>> Publisher::VerifyBySignedPolicy(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info)
	{
		if(_access_controller == nullptr)
		{
			return {AccessController::VerificationResult::Error, nullptr};
		}

		return _access_controller->VerifyBySignedPolicy(host_info, request_info);
	}

	std::tuple<AccessController::VerificationResult, std::shared_ptr<const SignedPolicy>> Publisher::VerifyBySignedPolicy(const std::shared_ptr<const ac::RequestInfo> &request_info)
	{
		if(_access_controller == nullptr)
		{
			return {AccessController::VerificationResult::Error, nullptr};
		}

		return _access_controller->VerifyBySignedPolicy(request_info);
	}

	std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> Publisher::SendCloseAdmissionWebhooks(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info)
	{
		if(_access_controller == nullptr)
		{
			return {AccessController::VerificationResult::Error, nullptr};
		}

		return _access_controller->SendCloseWebhooks(host_info, request_info);
	}

	std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> Publisher::SendCloseAdmissionWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info)
	{
		if(_access_controller == nullptr)
		{
			return {AccessController::VerificationResult::Error, nullptr};
		}

		return _access_controller->SendCloseWebhooks(request_info);
	}

	std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> Publisher::VerifyByAdmissionWebhooks(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info)
	{
		if(_access_controller == nullptr)
		{
			return {AccessController::VerificationResult::Error, nullptr};
		}

		return _access_controller->VerifyByWebhooks(host_info, request_info);
	}

	std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> Publisher::VerifyByAdmissionWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info)
	{
		if(_access_controller == nullptr)
		{
			return {AccessController::VerificationResult::Error, nullptr};
		}

		return _access_controller->VerifyByWebhooks(request_info);
	}

	std::shared_ptr<Session> Publisher::GetSession(const info::Session::Path &session_path)
	{
		auto application = GetApplicationById(session_path._application_id);
		if (application == nullptr)
		{
			return nullptr;
		}

		auto stream = application->GetStream(session_path._stream_id);
		if (stream == nullptr)
		{
			return nullptr;
		}

		return stream->GetSession(session_path._session_id);
	}
}  // namespace pub
