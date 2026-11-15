//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/physical_port/physical_port_manager.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ovt_publisher.h"

namespace
{
	constexpr int WAIT_TIMEOUT_MSEC = 5000;

	bool WaitUntil(const std::function<bool()> &condition)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(WAIT_TIMEOUT_MSEC);

		while (condition() == false)
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		return true;
	}

	// A connected loopback TCP client, closed on destruction
	class Client
	{
	public:
		explicit Client(uint16_t port)
			: _fd(::socket(AF_INET, SOCK_STREAM, 0))
		{
			sockaddr_in address{};
			address.sin_family		= AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port		= htons(port);

			_connected = (_fd >= 0) && (::connect(_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
		}

		~Client()
		{
			Close();
		}

		bool IsConnected() const
		{
			return _connected;
		}

		bool Send(const char *text)
		{
			const auto length = ::strlen(text);
			return ::send(_fd, text, length, MSG_NOSIGNAL) == static_cast<ssize_t>(length);
		}

		// The close sends a RST instead of a FIN
		bool SetResetOnClose()
		{
			linger reset{1, 0};
			return ::setsockopt(_fd, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0;
		}

		void Close()
		{
			if (_fd >= 0)
			{
				::close(std::exchange(_fd, -1));
			}
		}

	private:
		int _fd;
		bool _connected = false;
	};

	// Records, per connection, the order of the port events ('C' connected, 'D' data, 'X' disconnected),
	// and holds the worker in the first `OnDataReceived()` until `Release()`
	class GateObserver : public PhysicalPortObserver
	{
	public:
		~GateObserver() override
		{
			Release();
		}

		void OnConnected(const std::shared_ptr<ov::Socket> &remote) override
		{
			Append(remote, 'C');
		}

		void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override
		{
			Append(remote, 'D');

			if (data_count++ == 0)
			{
				_entered.set_value();
				_released_future.wait();
			}
		}

		void OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error) override
		{
			Append(remote, 'X');
			disconnected_count++;
		}

		// Ordered by socket ID, which follows the order the connections were accepted
		std::vector<std::string> Sequences()
		{
			std::lock_guard<std::mutex> lock(_mutex);
			std::vector<std::string> sequences;

			for (const auto &[id, events] : _events)
			{
				sequences.push_back(events);
			}

			return sequences;
		}

		bool WaitEntered()
		{
			return _entered.get_future().wait_for(std::chrono::milliseconds(WAIT_TIMEOUT_MSEC)) == std::future_status::ready;
		}

		void Release()
		{
			if (_is_released.exchange(true) == false)
			{
				_released.set_value();
			}
		}

	private:
		void Append(const std::shared_ptr<ov::Socket> &remote, char event)
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_events[remote->GetId()].push_back(event);
		}

	public:
		std::atomic<int> data_count{0};
		std::atomic<int> disconnected_count{0};

	private:
		std::mutex _mutex;
		std::map<ov::socket_id_t, std::string> _events;

		std::promise<void> _entered;
		std::promise<void> _released;
		std::shared_future<void> _released_future = _released.get_future().share();
		std::atomic<bool> _is_released{false};
	};
}  // namespace

class OvtPublisherRemoteTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		_publisher = std::make_shared<OvtPublisher>(cfg::Server(), nullptr);
	}

	size_t RemoteContextCount()
	{
		std::lock_guard<std::mutex> lock(_publisher->_remotes_lock);
		return _publisher->_remotes.size();
	}

protected:
	std::shared_ptr<OvtPublisher> _publisher;
};

// A client reset before the worker first polls it still has its connection reported before its bytes,
// and then its close, so it leaves no context behind
TEST_F(OvtPublisherRemoteTest, ClientResetBeforeItsConnectionIsReportedLeavesNoContext)
{
	auto port = PhysicalPortManager::GetInstance()->CreatePort("OvtTest", ov::SocketType::Tcp, ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0), 1);
	ASSERT_NE(port, nullptr);

	GateObserver gate;
	ASSERT_TRUE(port->AddObserver(_publisher.get()));
	ASSERT_TRUE(port->AddObserver(&gate));

	// Runs on every way out, including a failed `ASSERT_*`.
	// `DeletePort()` closes the port, which stops its workers before it drops the observers.
	std::shared_ptr<void> cleanup(nullptr, [&](void *) {
		gate.Release();
		PhysicalPortManager::GetInstance()->DeletePort(port);
	});

	sockaddr_in address{};
	socklen_t length = sizeof(address);
	ASSERT_EQ(::getsockname(port->GetSocket()->GetNativeHandle(), reinterpret_cast<sockaddr *>(&address), &length), 0);
	const auto listening_port = ntohs(address.sin_port);

	// The first client's bytes hold the worker inside the gate
	Client first(listening_port);
	ASSERT_TRUE(first.IsConnected());
	ASSERT_TRUE(first.Send("x"));
	ASSERT_TRUE(gate.WaitEntered());

	// Meanwhile the second client connects, sends and resets, all before the worker accepts it
	{
		Client second(listening_port);
		ASSERT_TRUE(second.IsConnected());
		ASSERT_TRUE(second.Send("y"));
		ASSERT_TRUE(second.SetResetOnClose());
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	gate.Release();
	ASSERT_TRUE(WaitUntil([&]() { return gate.data_count.load() == 2; }));

	ASSERT_TRUE(WaitUntil([&]() { return gate.disconnected_count.load() == 1; }));
	EXPECT_EQ(RemoteContextCount(), 1U);

	first.Close();
	ASSERT_TRUE(WaitUntil([&]() { return gate.disconnected_count.load() == 2; }));

	EXPECT_EQ(gate.Sequences(), (std::vector<std::string>{"CDX", "CDX"}));
	EXPECT_EQ(RemoteContextCount(), 0U);
}
