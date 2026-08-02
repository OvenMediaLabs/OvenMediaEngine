
#include "provider.h"
#include "provider_private.h"

#include <cinttypes>

namespace pvd
{
	namespace
	{
		// Leaves `PushStream::OnDataReceived()` even if it throws
		class ProcessingDataGuard
		{
		public:
			explicit ProcessingDataGuard(const std::shared_ptr<PushStream> &channel)
				: _channel(channel),
				  _entered(channel->BeginProcessingData())
			{
			}

			~ProcessingDataGuard()
			{
				if (_entered)
				{
					_channel->EndProcessingData();
				}
			}

			bool IsEntered() const
			{
				return _entered;
			}

		private:
			std::shared_ptr<PushStream> _channel;
			bool _entered;
		};
	}  // namespace

	PushProvider::PushProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: Provider(server_config, router)
	{
	}

	PushProvider::~PushProvider()
	{

	}

    bool PushProvider::Start()
    {
		_run_task_runner.store(true);
		_task_runner_thread = std::thread(&PushProvider::ChannelTaskRunner, this);

		ov::String thread_name = ov::String::FormatString("PTimer-%s", StringFromProviderType(GetProviderType()).CStr());
		pthread_setname_np(_task_runner_thread.native_handle(), thread_name.CStr());

        return Provider::Start();
    }

	bool PushProvider::Bind()
	{
		// Default no-op for push providers without their own listener split. Concrete providers
		// that hold sockets (WebRTC, MpegTs, Srt, Rtmp) override this to bind their listeners.
		return true;
	}

	bool PushProvider::Stop()
    {
		_run_task_runner.store(false);
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
		{
			// Marks the channel while `PushStream::OnDataReceived()` runs, so the time it spends there,
			// access control included, is not read as client silence. The received time is published
			// before the mark is cleared, so no reader sees a cleared mark with an older time.
			ProcessingDataGuard guard(channel);

			if (guard.IsEntered() == false)
			{
				// The channel is reserved for deletion, so nothing is left to hand data to.
				return false;
			}

			if (channel->OnDataReceived(data) == true)
			{
				channel->UpdateLastReceivedTime();
			}
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
		else
		{
			// The channel never joined an application.
			// `Application::DeleteStream()` did not run, so nothing tells the client it stopped.
			channel->CloseTransport();
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

		while (_run_task_runner.load() == true)
		{
			lock.lock();
			auto channels = _channels;
			lock.unlock();

			for (const auto &x : channels)
			{
				auto channel = x.second;

				// Read once, so nothing below pairs an elapsed time from one side of an `OnDataReceived()`
				// with a `PacketSilenceTimeoutMs` from the other.
				const auto state = channel->GetSilenceState();

				if (state.is_processing)
				{
					// A call is inside this channel, so the channel is not silent.
					continue;
				}

				if (state.timeout_ms == 0)
				{
					// If the packet silence timeout is 0, it means that the channel is not timed out.
					continue;
				}

				const auto elapsed_ms = static_cast<intmax_t>(state.elapsed_ms);
				const auto timeout_ms = static_cast<intmax_t>(state.timeout_ms);

				logtt("Checking channel %u, elapsed %" PRIdMAX " ms, timeout %" PRIdMAX " ms", channel->GetChannelId(),
					  elapsed_ms,
					  timeout_ms);

				if (state.IsSilentBeyondTimeout())
				{
					if (channel->TryBeginReaping(state) == false)
					{
						// The channel changed since `GetSilenceState()` read it. The next tick reads again.
						continue;
					}

					logtw("Channel %u is timed out, %" PRIdMAX " ms elapsed since last received, deleting it", channel->GetChannelId(), elapsed_ms);

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
}  // namespace pvd
