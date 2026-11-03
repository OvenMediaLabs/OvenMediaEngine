#include "origin_map_client.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace
{
	using namespace std::chrono_literals;
	const std::string kOrigin = "ovt://127.0.0.1:9000/app/stream";

	// Answer only AUTH, PING and GET. Leave one command unanswered while keeping
	// its TCP connection open; close it after six seconds so a regression cannot hang.
	class RedisPeer
	{
	public:
		explicit RedisPeer(const char *stall) : _stall(stall) {}

		~RedisPeer()
		{
			_stop = true;
			if (_thread.joinable())
			{
				_thread.join();
			}
			if (_listener >= 0)
			{
				close(_listener);
			}
		}

		bool Start()
		{
			_listener = socket(AF_INET, SOCK_STREAM, 0);
			if (_listener < 0)
			{
				return false;
			}
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if (bind(_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(_listener, 2) != 0)
			{
				return false;
			}
			socklen_t size = sizeof(address);
			if (getsockname(_listener, reinterpret_cast<sockaddr *>(&address), &size) != 0)
			{
				return false;
			}
			_port = ntohs(address.sin_port);
			_thread = std::thread([this] { Run(); });
			return true;
		}

		ov::String Host() const { return ov::String::FormatString("127.0.0.1:%u", _port); }
		bool Stalled() const { return _stalled; }
		int Connections() const { return _connections; }

	private:
		void Run()
		{
			while (_stop == false)
			{
				pollfd listener{_listener, POLLIN, 0};
				if (poll(&listener, 1, 100) <= 0)
				{
					continue;
				}
				int peer = accept(_listener, nullptr, nullptr);
				if (peer < 0)
				{
					return;
				}
				++_connections;
				timeval timeout{6, 0};
				if (setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
					setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0)
				{
					Serve(peer);
				}
				close(peer);
			}
		}

		void Serve(int peer)
		{
			std::unique_ptr<redisReader, decltype(&redisReaderFree)> reader(redisReaderCreate(), redisReaderFree);
			while (_stop == false)
			{
				void *parsed = nullptr;
				if (redisReaderGetReply(reader.get(), &parsed) != REDIS_OK)
				{
					return;
				}
				if (parsed == nullptr)
				{
					char buffer[1024];
					auto received = recv(peer, buffer, sizeof(buffer), 0);
					if (received <= 0 || redisReaderFeed(reader.get(), buffer, received) != REDIS_OK)
					{
						return;
					}
					continue;
				}
				std::unique_ptr<redisReply, decltype(&freeReplyObject)> request(static_cast<redisReply *>(parsed), freeReplyObject);
				ASSERT_EQ(request->type, REDIS_REPLY_ARRAY);
				ASSERT_GT(request->elements, 0u);
				ASSERT_EQ(request->element[0]->type, REDIS_REPLY_STRING);
				const std::string command = request->element[0]->str;
				if (command == _stall && _stalled == false)
				{
					_stalled = true;
					// A timed-out client closes this connection before reconnecting. Poll
					// notices that immediately so the replacement can be served normally.
					pollfd connection{peer, POLLIN, 0};
					poll(&connection, 1, 6000);
					return;
				}
				std::string reply;
				if (command == "AUTH")
				{
					reply = "+OK\r\n";
				}
				else if (command == "PING")
				{
					reply = "+PONG\r\n";
				}
				else
				{
					ASSERT_EQ(command, "GET");
					reply = "$" + std::to_string(kOrigin.size()) + "\r\n" + kOrigin + "\r\n";
				}
				ASSERT_EQ(send(peer, reply.data(), reply.size(), MSG_NOSIGNAL), static_cast<ssize_t>(reply.size()));
			}
		}

		const std::string _stall;
		int _listener = -1;
		uint16_t _port = 0;
		std::atomic<bool> _stop{false};
		std::atomic<bool> _stalled{false};
		std::atomic<int> _connections{0};
		std::thread _thread;
	};

	class OriginMapClientTimeout : public testing::TestWithParam<const char *> {};

	TEST_P(OriginMapClientTimeout, RecoversAfterAnUnansweredCommand)
	{
		RedisPeer peer(GetParam());
		ASSERT_TRUE(peer.Start());
		OriginMapClient client(peer.Host(), std::string(GetParam()) == "AUTH" ? "password" : "");
		ov::String origin;
		const bool ping = std::string(GetParam()) == "PING";
		if (ping)
		{
			ASSERT_EQ(client.GetOrigin("app/stream", origin), CommonErrorCode::SUCCESS);
		}

		const auto started = std::chrono::steady_clock::now();
		const auto result = client.GetOrigin("app/stream", origin);
		const auto elapsed = std::chrono::steady_clock::now() - started;
		ASSERT_TRUE(peer.Stalled());
		// Leave scheduling margin around the three-second timeout, but fail before
		// the peer's six-second fallback close can release an unbounded operation.
		EXPECT_LT(elapsed, 5s);
		EXPECT_EQ(result, ping ? CommonErrorCode::SUCCESS : CommonErrorCode::ERROR);

		ASSERT_EQ(client.GetOrigin("app/stream", origin), CommonErrorCode::SUCCESS);
		EXPECT_EQ(origin, kOrigin.c_str());
		EXPECT_EQ(peer.Connections(), 2);
	}

	INSTANTIATE_TEST_SUITE_P(Redis, OriginMapClientTimeout, testing::Values("AUTH", "PING", "GET"));
}
