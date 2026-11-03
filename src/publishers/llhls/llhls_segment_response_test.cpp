#include <gtest/gtest.h>
#include <config/items/items.h>

#define protected public
#include <config/config_manager.h>
#undef protected

#include <base/ovsocket/socket_pool/socket_pool.h>
#include <base/ovsocket/server_socket.h>
#include <base/publisher/session.h>
#include <base/publisher/stream.h>
#include <modules/access_control/access_controller.h>
#include <modules/http/server/http1/http1_request.h>
#include <modules/http/server/http1/http1_response.h>
#include <modules/http/server/http_connection.h>
#include <modules/http/server/http_exchange.h>
#include <base/info/dump.h>
#include <modules/dump/dump.h>
#include <monitoring/monitoring.h>
#include <modules/containers/bmff/fmp4_packager/duration_boundary_policy.h>
#include <modules/containers/bmff/fmp4_packager/fmp4_packager.h>
#include <modules/containers/bmff/fmp4_packager/synced_boundary_policy.h>
#include <modules/containers/webvtt/webvtt_packager.h>
#include "llhls_master_playlist.h"
#include "llhls_chunklist.h"
#include "llhls_keyframe_start_aligner.h"

// Keep test access local to these classes, after their dependencies are included.
#define private public
#include "llhls_session.h"
#include "llhls_stream.h"
#undef private
#include "llhls_application.h"

namespace
{
	class TestApplicationInfo : public info::Application
	{
	public:
		using info::Application::GetInvalidApplication;
	};

	class TestRequest : public http::svr::h1::Http1Request
	{
	public:
		TestRequest(const ov::String &uri, const std::shared_ptr<ov::ClientSocket> &socket) : Http1Request(socket)
		{
			_request_uri = uri;
			_parsed_uri = ov::Url::Parse(uri);
		}
	};

	// Exercise the real HTTP/1 payload writer; intercept only its transport.
	class TestResponse : public http::svr::h1::Http1Response
	{
	public:
		explicit TestResponse(const std::shared_ptr<ov::ClientSocket> &socket) : Http1Response(socket) {}
		ov::String body;

	private:
		int32_t SendHeader() override { return 1; }
		bool Send(const std::shared_ptr<const ov::Data> &data) override
		{
			body.Append(reinterpret_cast<const char *>(data->GetData()), data->GetLength());
			return true;
		}
	};

	class TestConnection : public http::svr::HttpConnection
	{
	public:
		explicit TestConnection(const std::shared_ptr<ov::ClientSocket> &socket) : HttpConnection(nullptr, socket) {}
		ov::String ToString() const override { return "segment test"; }
	};

	class TestExchange : public http::svr::HttpExchange
	{
	public:
		TestExchange(const ov::String &uri, const std::shared_ptr<ov::ClientSocket> &socket)
			: HttpExchange(std::make_shared<TestConnection>(socket)), request(std::make_shared<TestRequest>(uri, socket)), response(std::make_shared<TestResponse>(socket)) {}
		std::shared_ptr<http::svr::HttpRequest> GetRequest() const override { return request; }
		std::shared_ptr<http::svr::HttpResponse> GetResponse() const override { return response; }
		std::shared_ptr<TestRequest> request;
		std::shared_ptr<TestResponse> response;
	};

	class LLHlsSegmentResponse : public testing::TestWithParam<int64_t>
	{
	protected:
		void SetUp() override
		{
			previous_config = cfg::ConfigManager::GetInstance()->_server;
			cfg::ConfigManager::GetInstance()->_server = std::make_shared<cfg::Server>();
			// Satisfy transport invariants without listening or sending traffic.
			pool = ov::SocketPool::Create("llhls-segment-test", ov::SocketType::Tcp, false);
			ASSERT_TRUE(pool->Initialize(1));
			server_socket = pool->AllocSocket<ov::ServerSocket>(ov::SocketFamily::Inet, pool);
			ASSERT_NE(server_socket, nullptr);
			const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
			ASSERT_GE(fd, 0);
			client_socket = pool->AllocSocket<ov::ClientSocket>(ov::SocketFamily::Inet, server_socket,
				ov::SocketWrapper(ov::SocketType::Tcp, fd), ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0));
			if (client_socket == nullptr) ::close(fd);
			ASSERT_NE(client_socket, nullptr);

			auto app_info = TestApplicationInfo::GetInvalidApplication();
			app = std::make_shared<LLHlsApplication>(nullptr, app_info);
			stream = std::make_shared<LLHlsStream>(app, info::Stream(app_info, StreamSourceType::Ovt), true, 0);
			stream->SetState(pub::Stream::State::STARTED);
			auto track = std::make_shared<MediaTrack>();
			track->SetId(1);
			track->SetMediaType(cmn::MediaType::Video);
			track->SetTimeBase(1, 1000);
			stream->AddTrack(track);
			bmff::SegmentBoundaryPolicy::Config policy_config;
			policy_config.initial_segment_number = GetParam();
			storage = std::make_shared<bmff::FMP4Storage>(nullptr, track, bmff::FMP4Storage::Config{}, "test",
				std::make_shared<bmff::DurationBoundaryPolicy>(policy_config, GetParam() != 0));
			stream->_storage_map[1] = storage;
			session = LLHlsSession::Create(1, true, "test", app, stream, 0);
			ASSERT_NE(session, nullptr);
		}

		void TearDown() override
		{
			session.reset();
			stream.reset();
			app.reset();
			if (client_socket) pool->ReleaseSocket(client_socket);
			if (server_socket) pool->ReleaseSocket(server_socket);
			if (pool) pool->Uninitialize();
			cfg::ConfigManager::GetInstance()->_server = previous_config;
		}

		void Append(bool complete = false)
		{
			ASSERT_TRUE(storage->AppendMediaChunk(ov::String("media").ToData(false), timestamp, 500, true, complete));
			timestamp += 500;
		}

		std::shared_ptr<TestExchange> Exchange(const char *file)
		{
			return std::make_shared<TestExchange>(ov::String("http://localhost/app/stream/") + file, client_socket);
		}

		std::shared_ptr<cfg::Server> previous_config;
		std::shared_ptr<ov::SocketPool> pool;
		std::shared_ptr<ov::ServerSocket> server_socket;
		std::shared_ptr<ov::ClientSocket> client_socket;
		std::shared_ptr<LLHlsApplication> app;
		std::shared_ptr<LLHlsStream> stream;
		std::shared_ptr<LLHlsSession> session;
		std::shared_ptr<bmff::FMP4Storage> storage;
		int64_t timestamp = 0;
	};

	TEST_P(LLHlsSegmentResponse, InProgressSegmentIsNotFoundUntilCompleted)
	{
		const int64_t msn = GetParam();
		Append();
		Append();

		auto part = Exchange("part.m4s");
		session->ResponsePartialSegment(part, "part.m4s", 1, msn, 1, false);
		EXPECT_EQ(part->response->GetStatusCode(), http::StatusCode::OK);
		EXPECT_EQ(part->response->body, "media");

		auto in_progress = Exchange("segment.m4s");
		session->ResponseSegment(in_progress, "segment.m4s", 1, msn);
		EXPECT_EQ(in_progress->response->GetStatusCode(), http::StatusCode::NotFound);
		EXPECT_EQ(in_progress->response->body, "");

		Append(true);

		auto completed = Exchange("segment.m4s");
		session->ResponseSegment(completed, "segment.m4s", 1, msn);
		EXPECT_EQ(completed->response->GetStatusCode(), http::StatusCode::OK);
		EXPECT_EQ(completed->response->body, "mediamediamedia");
	}

	INSTANTIATE_TEST_SUITE_P(Numbering, LLHlsSegmentResponse, testing::Values(int64_t{0}, int64_t{293333333}));
} // namespace
