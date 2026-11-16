//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/ovsocket/socket_test_helpers.h>
#include <gtest/gtest.h>
#include <modules/physical_port/physical_port_manager.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ovt_publisher.h"

namespace
{
	using ome_test::ConnectAndSend;
	using ome_test::LocalPortOf;
	using ome_test::ScopeExit;
	using ome_test::WaitUntil;
	using ome_test::WorkerGate;

	// Records, per connection, the order of the port events ('C' connected, 'D' data, 'X' disconnected),
	// and runs `on_data` on every `OnDataReceived()`
	class EventObserver : public PhysicalPortObserver
	{
	public:
		explicit EventObserver(std::function<void()> on_data)
			: _on_data(std::move(on_data))
		{
		}

		void OnConnected(const std::shared_ptr<ov::Socket> &remote) override
		{
			Append(remote, 'C');
		}

		void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override
		{
			Append(remote, 'D');
			data_count++;
			_on_data();
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
		std::function<void()> _on_data;
		std::mutex _mutex;
		std::map<ov::socket_id_t, std::string> _events;
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

	// The first data callback holds the worker
	WorkerGate worker_gate;
	EventObserver observer(worker_gate.MakeCallback());
	ASSERT_TRUE(port->AddObserver(_publisher.get()));
	ASSERT_TRUE(port->AddObserver(&observer));

	// `DeletePort()` closes the port, which stops its workers before it drops the observers
	ScopeExit cleanup([&]() {
		worker_gate.Release();
		PhysicalPortManager::GetInstance()->DeletePort(port);
	});

	const auto listening_port = LocalPortOf(port->GetSocket());
	ASSERT_NE(listening_port, 0);

	auto first = ConnectAndSend(listening_port, "x");
	ASSERT_GE(first.Get(), 0);
	ASSERT_TRUE(worker_gate.WaitEntered());

	// Meanwhile the second client connects, sends and resets, all before the worker accepts it
	{
		auto second = ConnectAndSend(listening_port, "y");
		ASSERT_GE(second.Get(), 0);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	worker_gate.Release();
	ASSERT_TRUE(WaitUntil([&]() { return observer.data_count.load() == 2; }));

	ASSERT_TRUE(WaitUntil([&]() { return observer.disconnected_count.load() == 1; }));
	EXPECT_EQ(RemoteContextCount(), 1U);

	first.Reset();
	ASSERT_TRUE(WaitUntil([&]() { return observer.disconnected_count.load() == 2; }));

	EXPECT_EQ(observer.Sequences(), (std::vector<std::string>{"CDX", "CDX"}));
	EXPECT_EQ(RemoteContextCount(), 0U);
}
