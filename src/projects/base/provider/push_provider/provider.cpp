
#include "provider.h"
#include "provider_private.h"

namespace pvd
{
	PushProvider::PushProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: Provider(server_config, router)
	{
	}

	PushProvider::~PushProvider()
	{

	}

    bool PushProvider::Start()
    {
		_run_task_runner = true;
		_task_runner_thread = std::thread(&PushProvider::ChannelTaskRunner, this);

		ov::String thread_name = ov::String::FormatString("PTimer-%s", StringFromProviderType(GetProviderType()).CStr());
		pthread_setname_np(_task_runner_thread.native_handle(), thread_name.CStr());

        return Provider::Start();
    }

	bool PushProvider::Stop()
    {
		_run_task_runner = false;
		if(_task_runner_thread.joinable())
		{
			_task_runner_thread.join();
		}

        return Provider::Stop();
    }

	std::shared_ptr<PushApplication> PushProvider::GetApplicationByName(const info::VHostAppName &vhost_app_name)
	{
		return std::static_pointer_cast<PushApplication>(Provider::GetApplicationByName(vhost_app_name));
	}

	// To be interleaved mode, a channel must have application/stream and track informaiton
	bool PushProvider::PublishChannel(uint32_t channel_id, const info::VHostAppName &vhost_app_name, const std::shared_ptr<PushStream> &channel)
	{
		// Append the stream into the application
		auto application = std::dynamic_pointer_cast<PushApplication>(GetApplicationByName(vhost_app_name));
		if(application == nullptr)
		{
			logte("Cannot find application(%s) to publish interleaved channel", vhost_app_name.CStr());
			return false;
		}

		return application->JoinStream(channel);
	}

	std::shared_ptr<PushStream> PushProvider::GetChannel(uint32_t channel_id)
	{
		std::shared_lock<std::shared_mutex> lock(_channels_lock);
		// find stream by client_id
		auto it = _channels.find(channel_id);
		if(it == _channels.end())
		{
			return nullptr;
		}

		return it->second;
	}

	bool PushProvider::OnChannelCreated(uint32_t channel_id, const std::shared_ptr<pvd::PushStream> &channel)
	{
		std::lock_guard<std::shared_mutex> lock(_channels_lock);

		_channels.emplace(channel_id, channel);

		channel->SetPacketSilenceTimeoutMs(DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);

		return true;
	}

	bool PushProvider::OnDataReceived(uint32_t channel_id, const std::shared_ptr<const ov::Data> &data)
	{
		auto channel = GetChannel(channel_id);
		if(channel == nullptr)
		{
			return false;
		}

		// In the future, 
		// it may be necessary to send data to an application rather than sending it directly to a stream.
		if(channel->OnDataReceived(data) == true)
		{
			channel->UpdateLastReceivedTime();
		}

		return true;
	}

	bool PushProvider::OnChannelDeleted(const std::shared_ptr<pvd::PushStream> &channel)
	{
		if(channel == nullptr)
		{
			return false;
		}

		// Delete from stream_mold
		std::unique_lock<std::shared_mutex> lock(_channels_lock);
		
		if(_channels.erase(channel->GetChannelId()) == 0)
		{
			// probabliy, it was removed 
			logte("%d channel to be deleted cannot be found", channel->GetChannelId());
			return false;
		}

		lock.unlock();

		// Delete from Application
		if(channel->DoesBelongApplication())
		{
			auto application = channel->GetApplication();
			if(application == nullptr)
			{
				// Something wrong
				logte("Cannot find application to delete stream (%s)", channel->GetName().CStr());
				return false;
			}

			application->DeleteStream(channel);
		}

		return true;
	}

	bool PushProvider::OnChannelDeleted(uint32_t channel_id)
	{
		return OnChannelDeleted(GetChannel(channel_id));
	}

	bool PushProvider::OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application)
	{
		std::unique_lock<std::shared_mutex> lock(_channels_lock);

		auto it = _channels.begin();
		while(it != _channels.end())
		{
			auto channel = it->second;

			if(channel->GetApplication() == nullptr)
			{
				it ++;
				continue;
			}

			if(channel->GetApplication()->GetId() == application->GetId())
			{
				it = _channels.erase(it);
			}
			else
			{
				it ++;
			}
			
		}
		return true;
	}

	void PushProvider::ChannelTaskRunner()
	{
		ov::logger::ThreadHelper thread_helper;

		std::shared_lock<std::shared_mutex> lock(_channels_lock, std::defer_lock);

		while(_run_task_runner == true)
		{
			lock.lock();
			auto channels = _channels;
			lock.unlock();

			for (const auto &x : channels)
			{
				auto channel = x.second;

				if (channel->GetPacketSilenceTimeoutMs() == 0)
				{
					// If the packet silence timeout is 0, it means that the channel is not timed out.
					continue;
				}

				logtt("Checking channel %d, elapsed %d ms, timeout %d ms", channel->GetChannelId(), 
																			channel->GetElapsedMsSinceLastReceived(),
																			channel->GetPacketSilenceTimeoutMs());

				if (channel->GetElapsedMsSinceLastReceived() > channel->GetPacketSilenceTimeoutMs())
				{
					logtw("Channel %d is timed out, %d ms elapsed since last received, deleting it", channel->GetChannelId(), channel->GetElapsedMsSinceLastReceived());

					// Notify the channel timed out
					OnTimedOut(channel);
					
					// Delete the channel
					OnChannelDeleted(channel);
				}
			}

			// Sleep 100ms
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}