#include <gtest/gtest.h>
#include <config/items/items.h>
#include <sys/socket.h>
#include <unistd.h>

#define protected public
#include <config/config_manager.h>
#undef protected

#include <base/ovsocket/server_socket.h>
#include <base/ovsocket/socket_pool/socket_pool.h>

#include "server/http1/http1_request.h"
#include "server/http_connection.h"
#include "server/http_default_interceptor.h"
#include "server/http_exchange.h"

namespace
{
	class TestRequest : public http::svr::h1::Http1Request
	{
	public:
		TestRequest(http::Method method, const ov::String &uri, const std::shared_ptr<ov::ClientSocket> &socket)
			: Http1Request(socket), _method(method)
		{
			_request_uri = uri;
			_parsed_uri	 = ov::Url::Parse(uri);
		}

		http::Method GetMethod() const noexcept override
		{
			return _method;
		}

	private:
		http::Method _method;
	};

	class TestResponse : public http::svr::HttpResponse
	{
	public:
		explicit TestResponse(const std::shared_ptr<ov::ClientSocket> &socket)
			: HttpResponse(socket)
		{
		}

	private:
		int32_t SendHeader() override
		{
			return 1;
		}

		int32_t SendPayload() override
		{
			return GetResponseDataSize();
		}
	};

	class TestConnection : public http::svr::HttpConnection
	{
	public:
		explicit TestConnection(const std::shared_ptr<ov::ClientSocket> &socket)
			: HttpConnection(nullptr, socket)
		{
		}

		ov::String ToString() const override
		{
			return "routing test";
		}
	};

	class TestExchange : public http::svr::HttpExchange
	{
	public:
		TestExchange(http::Method method, const ov::String &uri, const std::shared_ptr<ov::ClientSocket> &socket)
			: HttpExchange(std::make_shared<TestConnection>(socket)),
			  request(std::make_shared<TestRequest>(method, uri, socket)),
			  response(std::make_shared<TestResponse>(socket))
		{
		}

		std::shared_ptr<http::svr::HttpRequest> GetRequest() const override
		{
			return request;
		}

		std::shared_ptr<http::svr::HttpResponse> GetResponse() const override
		{
			return response;
		}

		std::shared_ptr<TestRequest> request;
		std::shared_ptr<TestResponse> response;
	};

	std::shared_ptr<cfg::Server> ReplaceServerConfig(std::shared_ptr<cfg::Server> server)
	{
		auto config_manager = cfg::ConfigManager::GetInstance();
		ov::LockGuard lock(config_manager->_server_mutex);
		return std::exchange(config_manager->_server, std::move(server));
	}

	class DefaultInterceptorRouting : public testing::Test
	{
	protected:
		void SetUp() override
		{
			previous_config = ReplaceServerConfig(std::make_shared<cfg::Server>());

			// A response needs a client socket; nothing is sent through it
			pool = ov::SocketPool::Create("http-routing-test", ov::SocketType::Tcp, false);
			ASSERT_TRUE(pool->Initialize(1));
			server_socket = pool->AllocSocket<ov::ServerSocket>(ov::SocketFamily::Inet, pool);
			ASSERT_NE(server_socket, nullptr);
			const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
			ASSERT_GE(fd, 0);
			client_socket = pool->AllocSocket<ov::ClientSocket>(ov::SocketFamily::Inet, server_socket,
																ov::SocketWrapper(ov::SocketType::Tcp, fd), ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0));
			if (client_socket == nullptr)
			{
				::close(fd);
			}
			ASSERT_NE(client_socket, nullptr);

			interceptor.RegisterGet(R"(/api/items)", [this](const std::shared_ptr<http::svr::HttpExchange> &) {
				calls.push_back("get items");
				return http::svr::NextHandler::DoNotCall;
			});
			interceptor.RegisterPost(R"(/api/items)", [this](const std::shared_ptr<http::svr::HttpExchange> &) {
				calls.push_back("post items");
				return http::svr::NextHandler::DoNotCall;
			});
			interceptor.RegisterPost(R"(/api/upload)", [this](const std::shared_ptr<http::svr::HttpExchange> &) {
				calls.push_back("post upload");
				return http::svr::NextHandler::DoNotCall;
			});
		}

		void TearDown() override
		{
			if (client_socket != nullptr)
			{
				pool->ReleaseSocket(client_socket);
			}
			if (server_socket != nullptr)
			{
				pool->ReleaseSocket(server_socket);
			}
			if (pool != nullptr)
			{
				pool->Uninitialize();
			}
			ReplaceServerConfig(previous_config);
		}

		http::StatusCode Route(http::Method method, const char *path)
		{
			auto exchange = std::make_shared<TestExchange>(method, ov::String("http://localhost") + path, client_socket);
			EXPECT_EQ(interceptor.OnRequestCompleted(exchange), http::svr::InterceptorResult::Completed);
			return exchange->response->GetStatusCode();
		}

		std::shared_ptr<cfg::Server> previous_config;
		std::shared_ptr<ov::SocketPool> pool;
		std::shared_ptr<ov::ServerSocket> server_socket;
		std::shared_ptr<ov::ClientSocket> client_socket;
		http::svr::DefaultInterceptor interceptor;
		std::vector<std::string> calls;
	};
}  // namespace

TEST_F(DefaultInterceptorRouting, RunsOnlyTheHandlerForTheMethod)
{
	EXPECT_EQ(Route(http::Method::Post, "/api/items"), http::StatusCode::OK);
	EXPECT_EQ(calls, (std::vector<std::string>{"post items"}));
}

TEST_F(DefaultInterceptorRouting, AnotherMethodsPatternGives405)
{
	EXPECT_EQ(Route(http::Method::Get, "/api/upload"), http::StatusCode::MethodNotAllowed);
	EXPECT_EQ(Route(http::Method::Delete, "/api/items"), http::StatusCode::MethodNotAllowed);
	EXPECT_TRUE(calls.empty());
}

TEST_F(DefaultInterceptorRouting, NoPatternGives404)
{
	EXPECT_EQ(Route(http::Method::Get, "/api/missing"), http::StatusCode::NotFound);
	EXPECT_EQ(Route(http::Method::Post, "/other"), http::StatusCode::NotFound);
	EXPECT_TRUE(calls.empty());
}
