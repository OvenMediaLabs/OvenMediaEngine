//
// Created by getroot on 19. 12. 9.
//

#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/url.h>
#include <base/ovlibrary/semaphore.h>
#include <modules/ovt_packetizer/ovt_packet.h>
#include <modules/ovt_packetizer/ovt_packetizer.h>
#include <modules/ovt_packetizer/ovt_depacketizer.h>
#include <monitoring/monitoring.h>

#include <base/provider/pull_provider/application.h>
#include <base/provider/pull_provider/stream.h>

#define OVT_TIMEOUT_MSEC		3000

namespace pvd
{
	class OvtProvider;

	class OvtStream : public pvd::PullStream, public OvtPacketizerInterface
	{
	public:
		static std::shared_ptr<OvtStream> Create(const std::shared_ptr<pvd::PullApplication> &application, const uint32_t stream_id, const ov::String &stream_name,	const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);

		OvtStream(const std::shared_ptr<pvd::PullApplication> &application, const info::Stream &stream_info, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);
		~OvtStream() final;

		bool OnOvtPacketized(std::shared_ptr<OvtPacket> &packet) override;

		ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() override {
			return ProcessMediaEventTrigger::TRIGGER_EPOLL;
		}

		int GetFileDescriptorForDetectingEvent() override;
		// If this stream belongs to the Pull provider, 
		// this function is called periodically by the StreamMotor of application. 
		// Media data has to be processed here.
		PullStream::ProcessMediaResult ProcessMediaPacket() override;

	private:

		enum class ReceivePacketResult : uint8_t
		{
			COMPLETE,
			INCOMPLETE,
			DISCONNECTED,
			ERROR, 
			TIMEOUT,
			ALREADY_COMPLETED,
		};

		std::shared_ptr<pvd::OvtProvider> GetOvtProvider();

		bool StartStream(const std::shared_ptr<const ov::Url> &url) override; // Start
		bool RestartStream(const std::shared_ptr<const ov::Url> &url) override; // Failover
		bool StopStream() override; // Stop

		bool ConnectOrigin();
		bool RequestDescribe();
		bool ReceiveDescribe(uint32_t request_id);
		bool RequestPlay();
		bool ReceivePlay(uint32_t request_id);
		bool RequestStop();
		bool ReceiveStop(uint32_t request_id, const std::shared_ptr<OvtPacket> &packet);
		
		bool ReceivePacket(bool non_block = false);
		std::shared_ptr<ov::Data> ReceiveMessage();

		void Release();

		std::shared_ptr<ov::Socket> _client_socket = nullptr;
		std::shared_ptr<const ov::Url> _curr_url = nullptr;

		uint32_t _last_request_id;

		int64_t _origin_request_time_msec = 0;
		int64_t _origin_response_time_msec = 0;

		std::shared_mutex	_packetizer_lock;
		std::shared_ptr<OvtPacketizer>	_packetizer;
		OvtDepacketizer _depacketizer;
		std::shared_ptr<mon::StreamMetrics> _stream_metrics;
	};
}