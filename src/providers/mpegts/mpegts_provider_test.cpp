//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "mpegts_provider.h"

#include <base/mediarouter/mediarouter_interface.h>
#include <base/ovlibrary/crc.h>
#include <base/ovsocket/ovsocket.h>
#include <base/ovsocket/socket_pool/socket_pool.h>
#include <config/config.h>
#include <gtest/gtest.h>
#include <monitoring/monitoring.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

#include "modules/containers/mpegts/mpegts_packet.h"

// Drives `MpegTsProvider`'s datagram and timeout handlers directly, without physical ports.
// A UDP port takes one sender at a time, and these check that the port follows the channel registered for it:
// a port whose channel is gone must take the next sender.
namespace
{
	using mpegts::MPEGTS_HEADER_SIZE;
	using mpegts::MPEGTS_MIN_PACKET_SIZE;
	using mpegts::MPEGTS_SYNC_BYTE;

	// The source port of every test sender
	constexpr uint16_t REMOTE_PORT = 50000;

	class TestMpegTsProvider : public pvd::MpegTsProvider
	{
	public:
		using MpegTsProvider::MpegTsProvider;

		// `PhysicalPort` and `ChannelTaskRunner()` call these.
		using MpegTsProvider::OnDatagramReceived;
		using MpegTsProvider::OnTimedOut;
		using PushProvider::OnChannelDeleted;
		// The tests inspect channels and ports through these.
		using MpegTsProvider::GetStreamPortItem;
		using PushProvider::GetChannel;
		using PushProvider::GetChannelId;
		// The orchestrator calls these.
		using PushProvider::OnCreateApplication;
		using PushProvider::OnDeleteApplication;

		// `Bind()` starts the timeout thread along with the physical ports, which these tests do not create.
		bool StartChannelTaskRunner()
		{
			return PushProvider::Start();
		}
	};

	// The provider registers its applications with the router before storing them,
	// so a channel cannot reach its application without one.
	class StubMediaRouter : public MediaRouterInterface
	{
	public:
		CommonErrorCode MirrorStream(std::shared_ptr<MediaRouterStreamTap> &stream_tap, const info::VHostAppName &vhost_app_name,
									 const ov::String &stream_name, MirrorPosition position) override
		{
			return CommonErrorCode::SUCCESS;
		}

		CommonErrorCode UnmirrorStream(const std::shared_ptr<MediaRouterStreamTap> &stream_tap) override
		{
			return CommonErrorCode::SUCCESS;
		}

		bool RegisterConnectorApp(const info::Application &application_info,
								  const std::shared_ptr<MediaRouterApplicationConnector> &application_connector) override
		{
			return true;
		}

		bool UnregisterConnectorApp(const info::Application &application_info,
									const std::shared_ptr<MediaRouterApplicationConnector> &application_connector) override
		{
			return true;
		}

		bool RegisterObserverApp(const info::Application &application_info,
								 const std::shared_ptr<MediaRouterApplicationObserver> &application_observer) override
		{
			return true;
		}

		bool UnregisterObserverApp(const info::Application &application_info,
								   const std::shared_ptr<MediaRouterApplicationObserver> &application_observer) override
		{
			return true;
		}
	};

	// Both constructors are reachable only from a subclass,
	// because normally the orchestrator and the concrete providers are the ones that build these.
	class TestApplicationInfo : public info::Application
	{
	public:
		TestApplicationInfo(const info::Host &host_info, const info::VHostAppName &vhost_app_name,
							const cfg::vhost::app::Application &app_config)
			: info::Application(host_info, 1, vhost_app_name, app_config, false)
		{
		}
	};

	// A TS packet whose payload is exactly `payload`, padded in front by an adaptation field of stuffing bytes.
	std::vector<uint8_t> MakePacket(uint16_t pid, bool pusi, uint8_t cc, const std::vector<uint8_t> &payload)
	{
		std::vector<uint8_t> packet(MPEGTS_MIN_PACKET_SIZE, 0xFF);

		const size_t adaptation_size = MPEGTS_MIN_PACKET_SIZE - MPEGTS_HEADER_SIZE - payload.size();

		packet[0]					 = MPEGTS_SYNC_BYTE;
		packet[1]					 = static_cast<uint8_t>((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
		packet[2]					 = static_cast<uint8_t>(pid & 0xFF);
		packet[3]					 = static_cast<uint8_t>(((adaptation_size > 0) ? 0x30 : 0x10) | (cc & 0x0F));

		if (adaptation_size > 0)
		{
			packet[4] = static_cast<uint8_t>(adaptation_size - 1);
			if (adaptation_size > 1)
			{
				packet[5] = 0x00;
			}
		}

		std::copy(payload.begin(), payload.end(), packet.begin() + MPEGTS_HEADER_SIZE + adaptation_size);

		return packet;
	}

	void AppendCrc(std::vector<uint8_t> *section)
	{
		const auto crc = ov::CRC::Crc32Mpeg2(section->data(), section->size());
		section->insert(section->end(), {static_cast<uint8_t>(crc >> 24), static_cast<uint8_t>(crc >> 16),
										 static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc)});
	}

	// PAT and PMT of program 1 carrying only MPEG-2 video and AC-3 audio, neither of which OME supports
	std::shared_ptr<ov::Data> MakeUnsupportedProgram()
	{
		std::vector<uint8_t> pat = {
			// `table_id`=0, `section_syntax_indicator`=1, `section_length`=13
			0x00, 0xB0, 0x0D,
			// `transport_stream_id`=1, version=0, current_next=1, `section_number`=0, `last_section_number`=0
			0x00, 0x01, 0xC1, 0x00, 0x00,
			// `program_number`=1, `program_map_PID`=0x1000
			0x00, 0x01, 0xF0, 0x00};
		AppendCrc(&pat);

		std::vector<uint8_t> pmt = {
			// `table_id`=2, `section_syntax_indicator`=1, `section_length`=23
			0x02, 0xB0, 0x17,
			// `program_number`=1, version=0, current_next=1, `section_number`=0, `last_section_number`=0
			0x00, 0x01, 0xC1, 0x00, 0x00,
			// `PCR_PID`=0x0100, `program_info_length`=0
			0xE1, 0x00, 0xF0, 0x00,
			// `stream_type`=MPEG-2 video, `elementary_PID`=0x0100, `ES_info_length`=0
			0x02, 0xE1, 0x00, 0xF0, 0x00,
			// `stream_type`=AC-3, `elementary_PID`=0x0101, `ES_info_length`=0
			0x81, 0xE1, 0x01, 0xF0, 0x00};
		AppendCrc(&pmt);

		pat.insert(pat.begin(), 0x00);
		pmt.insert(pmt.begin(), 0x00);

		auto data		  = std::make_shared<ov::Data>();
		const auto pat_ts = MakePacket(0x0000, true, 0, pat);
		const auto pmt_ts = MakePacket(0x1000, true, 0, pmt);
		data->Append(pat_ts.data(), pat_ts.size());
		data->Append(pmt_ts.data(), pmt_ts.size());

		return data;
	}

	// A null packet, which any sender may put on the wire and which creates no track
	std::shared_ptr<ov::Data> MakeNullPacket()
	{
		const auto packet = MakePacket(0x1FFF, false, 0, std::vector<uint8_t>(MPEGTS_MIN_PACKET_SIZE - MPEGTS_HEADER_SIZE, 0xFF));
		return std::make_shared<ov::Data>(packet.data(), packet.size());
	}

	// A UDP port number that is free on both loopback addresses, for the two sockets that share it.
	// `ipv6_available` is false when the host has no IPv6 loopback; the port is then free on IPv4 only.
	uint16_t FindFreePort(bool *ipv6_available)
	{
		for (int attempt = 0; attempt < 32; attempt++)
		{
			const int fd4 = ::socket(AF_INET, SOCK_DGRAM, 0);
			sockaddr_in address4{};
			address4.sin_family		 = AF_INET;
			address4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			socklen_t length		 = sizeof(address4);

			if ((::bind(fd4, reinterpret_cast<sockaddr *>(&address4), sizeof(address4)) != 0) ||
				(::getsockname(fd4, reinterpret_cast<sockaddr *>(&address4), &length) != 0))
			{
				::close(fd4);
				continue;
			}

			const int fd6 = ::socket(AF_INET6, SOCK_DGRAM, 0);
			sockaddr_in6 address6{};
			address6.sin6_family = AF_INET6;
			address6.sin6_addr	 = in6addr_loopback;
			address6.sin6_port	 = address4.sin_port;
			const bool bound6	 = (fd6 >= 0) && (::bind(fd6, reinterpret_cast<sockaddr *>(&address6), sizeof(address6)) == 0);
			const int error6	 = errno;

			if (fd6 >= 0)
			{
				::close(fd6);
			}
			::close(fd4);

			if (bound6 || (error6 != EADDRINUSE))
			{
				*ipv6_available = bound6;
				return ntohs(address4.sin_port);
			}
		}

		return 0;
	}

	std::string WriteTempFile(const std::string &dir, const std::string &name, const std::string &body)
	{
		const auto path = dir + "/" + name;
		std::ofstream out(path);
		out << body;
		return path;
	}

	class MpegTsProviderPortTest : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			constexpr int SOCKET_ATTEMPTS = 8;

			_pool						  = ov::SocketPool::Create("test-mpegts", ov::SocketType::Udp, false);
			ASSERT_NE(_pool, nullptr);
			ASSERT_TRUE(_pool->Initialize(1));

			// Another process may take the probed port before the sockets bind it, so a failed bind probes again
			for (int attempt = 0; (attempt < SOCKET_ATTEMPTS) && (_socket_v4 == nullptr); attempt++)
			{
				bool ipv6_available = false;
				_port				= FindFreePort(&ipv6_available);
				if (_port == 0)
				{
					continue;
				}

				_socket_v4 = CreateSocket(ov::SocketFamily::Inet, "127.0.0.1");
				if ((_socket_v4 == nullptr) || (ipv6_available == false))
				{
					continue;
				}

				_socket_v6 = CreateSocket(ov::SocketFamily::Inet6, "::1");
				if (_socket_v6 == nullptr)
				{
					_socket_v4->Close();
					_socket_v4 = nullptr;
				}
			}
			ASSERT_NE(_socket_v4, nullptr);

			char dir[] = "/tmp/ome_mpegts_provider_test_XXXXXX";
			ASSERT_NE(::mkdtemp(dir), nullptr);
			_temp_dir = dir;

			ASSERT_TRUE(LoadServerConfig());
			ASSERT_TRUE(LoadApplicationConfig());

			MonitorInstance->OnServerStarted(_server_config);
			ASSERT_TRUE(MonitorInstance->OnHostCreated(*_host_info));
			ASSERT_TRUE(MonitorInstance->OnApplicationCreated(*_application_info));

			_provider = std::make_shared<TestMpegTsProvider>(*_server_config, std::make_shared<StubMediaRouter>());
			ASSERT_TRUE(_provider->Start());
			ASSERT_TRUE(_provider->OnCreateApplication(*_application_info));
			ASSERT_NE(_provider->GetApplicationByName(_application_info->GetVHostAppName()), nullptr);
		}

		void TearDown() override
		{
			if (_provider != nullptr)
			{
				_provider->Stop();
			}

			if (_application_info != nullptr)
			{
				MonitorInstance->OnApplicationDeleted(*_application_info);
			}

			if (_host_info != nullptr)
			{
				MonitorInstance->OnHostDeleted(*_host_info);
			}

			for (const auto &socket : {_socket_v4, _socket_v6})
			{
				if (socket != nullptr)
				{
					socket->Close();
				}
			}

			if (_pool != nullptr)
			{
				_pool->Uninitialize();
			}

			if (_temp_dir.empty() == false)
			{
				::remove((_temp_dir + "/Server.xml").c_str());
				::remove((_temp_dir + "/VHost.xml").c_str());
				::rmdir(_temp_dir.c_str());
			}
		}

		void Send(const std::shared_ptr<ov::DatagramSocket> &socket, const std::shared_ptr<const ov::Data> &data)
		{
			const auto remote = ov::SocketAddress::CreateAndGetFirst(
				(socket->GetLocalAddress()->GetFamily() == ov::SocketFamily::Inet6) ? "::1" : "127.0.0.1", REMOTE_PORT);

			_provider->OnDatagramReceived(socket, ov::SocketAddressPair(*socket->GetLocalAddress(), remote), data);
		}

		std::shared_ptr<pvd::PushStream> GetChannel(const std::shared_ptr<ov::Socket> &socket)
		{
			return _provider->GetChannel(TestMpegTsProvider::GetChannelId(socket));
		}

		// What `ChannelTaskRunner()` does for a channel it finds silent, split so a test can run something in between
		bool BeginTimeout(const std::shared_ptr<pvd::PushStream> &channel)
		{
			if (channel->TryBeginReaping(channel->GetSilenceState()) == false)
			{
				return false;
			}

			_provider->OnTimedOut(channel);
			return true;
		}

		// What the orchestrator does when the application is deleted and created again with the same name
		bool RecreateApplication()
		{
			return _provider->OnDeleteApplication(*_application_info) && _provider->OnCreateApplication(*_application_info);
		}

		uint16_t _port = 0;
		std::shared_ptr<TestMpegTsProvider> _provider;
		std::shared_ptr<ov::DatagramSocket> _socket_v4;
		// `nullptr` when the host has no IPv6 loopback
		std::shared_ptr<ov::DatagramSocket> _socket_v6;

	private:
		bool LoadServerConfig()
		{
			WriteTempFile(_temp_dir, "Server.xml",
						  "<Server version=\"8\"><Name>test</Name><Type>origin</Type><IP>*</IP>"
						  "<Bind><Providers><MPEGTS><Port>" +
							  std::to_string(_port) +
							  "/udp</Port></MPEGTS></Providers></Bind></Server>");

			try
			{
				cfg::DataSource data_source(cfg::DataType::Xml, _temp_dir.c_str(), "Server.xml", "Server");
				_server_config = std::make_shared<cfg::Server>();
				_server_config->SetItemName("Server");
				_server_config->FromDataSource(data_source);
			}
			catch (const cfg::ConfigError &error)
			{
				ADD_FAILURE() << error.What();
				return false;
			}

			return true;
		}

		bool LoadApplicationConfig()
		{
			const auto path = WriteTempFile(_temp_dir, "VHost.xml",
											"<VirtualHost><Name>default</Name>"
											"<Host><Names><Name>*</Name></Names></Host>"
											"<Applications><Application><Name>app</Name><Type>live</Type>"
											"<Providers><MPEGTS><StreamMap><Stream><Name>stream</Name><Port>" +
												std::to_string(_port) +
												"</Port></Stream></StreamMap></MPEGTS></Providers>"
												"</Application></Applications></VirtualHost>");

			cfg::vhost::VirtualHost vhost_config;

			try
			{
				cfg::DataSource data_source(cfg::DataType::Xml, path.c_str(), cfg::ItemName("VirtualHost"));
				vhost_config.FromDataSource("VirtualHost", cfg::ItemName("VirtualHost"), data_source);
			}
			catch (const cfg::ConfigError &error)
			{
				ADD_FAILURE() << error.What();
				return false;
			}

			const auto app_list = vhost_config.GetApplicationList();
			if (app_list.empty())
			{
				return false;
			}

			_host_info		  = std::make_shared<info::Host>("test-server", "test-server-id", vhost_config);
			_application_info = std::make_shared<TestApplicationInfo>(*_host_info, info::VHostAppName("default", "app"), app_list[0]);

			return true;
		}

		std::shared_ptr<ov::DatagramSocket> CreateSocket(ov::SocketFamily family, const char *host)
		{
			auto socket = _pool->AllocSocket<ov::DatagramSocket>(family);
			if (socket == nullptr)
			{
				return nullptr;
			}

			if (socket->Prepare(ov::SocketAddress::CreateAndGetFirst(host, _port), nullptr, nullptr) == false)
			{
				return nullptr;
			}

			return socket;
		}

		std::string _temp_dir;
		std::shared_ptr<cfg::Server> _server_config;
		std::shared_ptr<info::Host> _host_info;
		std::shared_ptr<TestApplicationInfo> _application_info;
		std::shared_ptr<ov::SocketPool> _pool;
	};
}  // namespace

// A program with no supported stream fails to publish. Its channel must still leave the port,
// or the port refuses every later sender until the application is deleted.
TEST_F(MpegTsProviderPortTest, AChannelThatFailedToPublishFreesThePort)
{
	constexpr auto WAIT_LIMIT = std::chrono::seconds(6);

	ASSERT_TRUE(_provider->StartChannelTaskRunner());

	Send(_socket_v4, MakeUnsupportedProgram());

	auto failed = GetChannel(_socket_v4);
	ASSERT_NE(failed, nullptr);
	ASSERT_FALSE(failed->IsPublished());
	ASSERT_EQ(failed->GetState(), pvd::Stream::State::STOPPED);

	// The sender keeps sending, as an encoder does after it has been fixed
	const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
	std::shared_ptr<pvd::PushStream> channel;

	while (std::chrono::steady_clock::now() < deadline)
	{
		Send(_socket_v4, MakeNullPacket());

		channel = GetChannel(_socket_v4);
		if ((channel != nullptr) && (channel != failed))
		{
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	ASSERT_NE(channel, nullptr);
	EXPECT_NE(channel.get(), failed.get());
	EXPECT_EQ(channel->GetState(), pvd::Stream::State::PLAYING);
}

// A sender that stops before completing PAT/PMT never publishes, and UDP has no disconnect to end its channel.
// The channel must still time out,
// or the port refuses the senders on its other socket until the application is deleted.
TEST_F(MpegTsProviderPortTest, AChannelThatStopsBeforePublishingFreesThePort)
{
	constexpr auto WAIT_LIMIT = std::chrono::seconds(6);

	ASSERT_TRUE(_provider->StartChannelTaskRunner());

	Send(_socket_v4, MakeNullPacket());

	auto stalled = GetChannel(_socket_v4);
	ASSERT_NE(stalled, nullptr);
	ASSERT_FALSE(stalled->IsPublished());
	EXPECT_EQ(stalled->GetPacketSilenceTimeoutMs(), pvd::MpegTsProvider::PRE_PUBLISH_PACKET_SILENCE_TIMEOUT_MS);

	const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
	while ((GetChannel(_socket_v4) != nullptr) && (std::chrono::steady_clock::now() < deadline))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	ASSERT_EQ(GetChannel(_socket_v4), nullptr);
	EXPECT_EQ(_provider->GetStreamPortItem(_port)->GetClientId(), pvd::channel_id_t{});

	// The other socket's sender, or a new sender on the same socket when there is no IPv6 loopback, takes the port
	const auto &next_socket = (_socket_v6 != nullptr) ? _socket_v6 : _socket_v4;
	Send(next_socket, MakeNullPacket());

	auto channel = GetChannel(next_socket);
	ASSERT_NE(channel, nullptr);
	EXPECT_NE(channel.get(), stalled.get());
}

// A datagram can arrive after the timeout let go of the port but before the timed-out channel is deleted.
// Its channel cannot register then, since the channel ID is still taken.
// The port must take the sender once the ID is free.
TEST_F(MpegTsProviderPortTest, ADatagramDuringTimeoutCleanupDoesNotStrandThePort)
{
	Send(_socket_v4, MakeNullPacket());

	auto timed_out = GetChannel(_socket_v4);
	ASSERT_NE(timed_out, nullptr);

	ASSERT_TRUE(BeginTimeout(timed_out));

	Send(_socket_v4, MakeNullPacket());
	EXPECT_EQ(GetChannel(_socket_v4).get(), timed_out.get());

	ASSERT_TRUE(_provider->OnChannelDeleted(timed_out));

	Send(_socket_v4, MakeNullPacket());

	auto channel = GetChannel(_socket_v4);
	ASSERT_NE(channel, nullptr);
	EXPECT_NE(channel.get(), timed_out.get());
}

// Two sockets on one port number (IPv4 and IPv6) run on separate threads.
// When their first datagrams arrive together, only one of them may take the port.
TEST_F(MpegTsProviderPortTest, OnlyOneSocketOfAPortTakesItWhenSendersArriveTogether)
{
	constexpr int ITERATIONS = 300;

	if (_socket_v6 == nullptr)
	{
		GTEST_SKIP() << "No IPv6 loopback for the port's second socket";
	}

	int both_taken = 0;

	for (int iteration = 0; iteration < ITERATIONS; iteration++)
	{
		std::atomic<int> ready = 0;

		auto send_together	   = [&](const std::shared_ptr<ov::DatagramSocket> &socket) {
			ready++;
			while (ready.load() < 2)
			{
			}
			Send(socket, MakeNullPacket());
		};

		std::thread thread_v4(send_together, _socket_v4);
		std::thread thread_v6(send_together, _socket_v6);
		thread_v4.join();
		thread_v6.join();

		auto channel_v4 = GetChannel(_socket_v4);
		auto channel_v6 = GetChannel(_socket_v6);

		ASSERT_TRUE((channel_v4 != nullptr) || (channel_v6 != nullptr));

		if ((channel_v4 != nullptr) && (channel_v6 != nullptr))
		{
			both_taken++;
		}

		for (const auto &channel : {channel_v4, channel_v6})
		{
			if (channel != nullptr)
			{
				ASSERT_TRUE(BeginTimeout(channel));
				ASSERT_TRUE(_provider->OnChannelDeleted(channel));
			}
		}
	}

	EXPECT_EQ(both_taken, 0);
}

// The test above needs the two senders to meet inside the port's check and take,
// which a busy or single-CPU host may never arrange.
// This one holds the first registration open for 50 ms after the second sender starts,
// so a port that skips its lock is caught unless that sender is delayed past the 50 ms.
TEST(MpegTsStreamPortItemTest, APortIsNotTakenWhileItsChannelRegisters)
{
	pvd::MpegTsStreamPortItem port_item(ov::SocketType::Udp, 4000, {});
	port_item.AttachToApplication(info::VHostAppName("#default", "app"), "stream");

	const auto generation = port_item.GetAttachment().generation;
	const pvd::channel_id_t first_id(1);
	const pvd::channel_id_t second_id(2);

	std::atomic<bool> registering		= false;
	std::atomic<bool> second_started	= false;
	std::atomic<bool> second_registered = false;
	bool second_taken					= false;

	std::thread second([&]() {
		while (registering.load() == false)
		{
		}

		second_started = true;
		second_taken   = port_item.ConnectClient(second_id, generation, [&]() {
			second_registered = true;
			return true;
		});
	});

	const bool first_taken = port_item.ConnectClient(first_id, generation, [&]() {
		registering = true;
		while (second_started.load() == false)
		{
		}

		// Gives the second sender time to reach the port while this registration is still open
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		return true;
	});

	second.join();

	EXPECT_TRUE(first_taken);
	EXPECT_FALSE(second_taken);
	EXPECT_FALSE(second_registered.load());
	EXPECT_EQ(port_item.GetClientId(), first_id);
}

// A channel that has not published yet does not belong to the application,
// so deleting the application has to delete it too.
// Otherwise it keeps the channel ID, and the port refuses its socket.
TEST_F(MpegTsProviderPortTest, AnUnpublishedChannelDoesNotOutliveItsApplication)
{
	Send(_socket_v4, MakeNullPacket());

	auto previous = GetChannel(_socket_v4);
	ASSERT_NE(previous, nullptr);
	ASSERT_FALSE(previous->IsPublished());

	ASSERT_TRUE(RecreateApplication());

	Send(_socket_v4, MakeNullPacket());

	auto channel = GetChannel(_socket_v4);
	ASSERT_NE(channel, nullptr);
	EXPECT_NE(channel.get(), previous.get());
}

// An `OnDataReceived()` can still be inside an unpublished channel when its application is deleted.
// Deleting the application must wait for that call to leave and reserve the channel,
// or the call could publish the channel into the recreated application after it left `_channels`.
TEST_F(MpegTsProviderPortTest, AChannelBeingProcessedCannotPublishAfterItsApplicationIsDeleted)
{
	Send(_socket_v4, MakeNullPacket());

	auto processing = GetChannel(_socket_v4);
	ASSERT_NE(processing, nullptr);
	ASSERT_FALSE(processing->IsPublished());

	// Holds the channel the way `PushProvider::OnDataReceived()` does while a datagram is inside it
	ASSERT_TRUE(processing->BeginProcessingData());

	std::atomic<bool> recreated = false;
	std::thread recreate([&]() {
		EXPECT_TRUE(RecreateApplication());
		recreated = true;
	});

	// Gives the deletion time to finish if it does not wait for the call inside the channel
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	EXPECT_FALSE(recreated.load());

	processing->EndProcessingData();
	recreate.join();

	// The channel was reserved before it was deleted, so it takes no more data and cannot publish
	EXPECT_EQ(GetChannel(_socket_v4), nullptr);
	EXPECT_FALSE(processing->BeginProcessingData());

	Send(_socket_v4, MakeNullPacket());

	auto channel = GetChannel(_socket_v4);
	ASSERT_NE(channel, nullptr);
	EXPECT_NE(channel.get(), processing.get());
}

// Deleting the application waits only so long for a call inside the channel,
// since the wait blocks every application create and delete.
// A channel still in use after that stays registered, and the silence timeout deletes it.
TEST_F(MpegTsProviderPortTest, AChannelStillInUseAfterTheWaitIsLeftToTheTimeout)
{
	constexpr auto WAIT_LIMIT = std::chrono::seconds(6);

	ASSERT_TRUE(_provider->StartChannelTaskRunner());

	Send(_socket_v4, MakeNullPacket());

	auto busy = GetChannel(_socket_v4);
	ASSERT_NE(busy, nullptr);
	ASSERT_TRUE(busy->BeginProcessingData());

	ASSERT_TRUE(RecreateApplication());
	EXPECT_EQ(GetChannel(_socket_v4).get(), busy.get());

	busy->EndProcessingData();

	const auto deadline = std::chrono::steady_clock::now() + WAIT_LIMIT;
	while ((GetChannel(_socket_v4) != nullptr) && (std::chrono::steady_clock::now() < deadline))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	ASSERT_EQ(GetChannel(_socket_v4), nullptr);

	Send(_socket_v4, MakeNullPacket());

	auto channel = GetChannel(_socket_v4);
	ASSERT_NE(channel, nullptr);
	EXPECT_NE(channel.get(), busy.get());
}

// The channels of a UDP port share the ID of its socket.
// A timeout pass may time out a replaced channel after a newer one took the port with the same ID.
// That timeout must leave the newer channel and its port alone.
// The owner check does not depend on how the replaced channel got there, so this calls `OnTimedOut()` directly.
TEST_F(MpegTsProviderPortTest, ALateTimeoutOfAReplacedChannelKeepsItsSuccessor)
{
	Send(_socket_v4, MakeNullPacket());

	auto replaced = GetChannel(_socket_v4);
	ASSERT_NE(replaced, nullptr);

	ASSERT_TRUE(RecreateApplication());

	Send(_socket_v4, MakeNullPacket());

	auto successor = GetChannel(_socket_v4);
	ASSERT_NE(successor, nullptr);
	ASSERT_NE(successor.get(), replaced.get());

	_provider->OnTimedOut(replaced);

	// Both channels carry the socket's ID, so only the owner check keeps the timeout from freeing the port
	EXPECT_EQ(_provider->GetStreamPortItem(_port)->GetClientId(), TestMpegTsProvider::GetChannelId(_socket_v4));

	EXPECT_FALSE(_provider->OnChannelDeleted(replaced));
	EXPECT_EQ(GetChannel(_socket_v4).get(), successor.get());

	// The port still belongs to the successor, so the other socket's sender is refused
	if (_socket_v6 != nullptr)
	{
		Send(_socket_v6, MakeNullPacket());
		EXPECT_EQ(GetChannel(_socket_v6), nullptr);
	}
}
