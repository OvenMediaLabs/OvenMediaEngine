//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <base/info/host.h>
#include <base/mediarouter/media_buffer.h>
#include <base/ovsocket/server_socket.h>
#include <base/ovsocket/socket_pool/socket_pool.h>
#include <config/config.h>
#include <config/config_manager.h>
#include <modules/bitstream/aac/audio_specific_config.h>
#include <modules/http/server/http1/http1_request.h>
#include <modules/http/server/http1/http1_response.h>
#include <modules/http/server/http_connection.h>
#include <modules/http/server/http_exchange.h>

#include "llhls_application.h"
#include "llhls_session.h"
#include "llhls_stream.h"

namespace
{
	constexpr int32_t kTrackId = 1;
	constexpr int32_t kSampleRate = 48000;
	constexpr int64_t kFrameSamples = 1024;
	constexpr int kMaxFrames = 1000;

	// AAC-LC, 48 kHz, stereo
	const uint8_t kAudioSpecificConfig[] = {0x11, 0x90};

	class TestApplicationInfo : public info::Application
	{
	public:
		TestApplicationInfo(const info::Host &host_info, const cfg::vhost::app::Application &app_config)
			: info::Application(host_info, 1, info::VHostAppName("default", "app"), app_config, false)
		{
		}
	};

	// An origin-mode LL-HLS application with 1 s segments of 0.2 s parts
	bool ParseVirtualHost(bool server_time_numbering, cfg::vhost::VirtualHost *vhost_config)
	{
		char path[]	 = "/tmp/ome_llhls_segment_test_XXXXXX";
		const int fd = ::mkstemp(path);
		if (fd < 0)
		{
			return false;
		}
		::close(fd);

		{
			std::ofstream out(path);
			out << "<VirtualHost>"
				<< "<Name>default</Name>"
				<< "<Host><Names><Name>*</Name></Names></Host>"
				<< "<Applications><Application>"
				<< "<Name>app</Name><Type>live</Type>"
				<< "<Publishers><LLHLS>"
				<< "<OriginMode>true</OriginMode>"
				<< "<SegmentDuration>1</SegmentDuration>"
				<< "<ChunkDuration>0.2</ChunkDuration>"
				<< "<ServerTimeBasedSegmentNumbering>" << (server_time_numbering ? "true" : "false") << "</ServerTimeBasedSegmentNumbering>"
				<< "</LLHLS></Publishers>"
				<< "</Application></Applications>"
				<< "</VirtualHost>";
		}

		bool parsed = false;

		try
		{
			cfg::DataSource data_source(cfg::DataType::Xml, path, cfg::ItemName("VirtualHost"));
			vhost_config->FromDataSource("VirtualHost", cfg::ItemName("VirtualHost"), data_source);
			parsed = (vhost_config->GetApplicationList().empty() == false);
		}
		catch (const cfg::ConfigError &)
		{
			parsed = false;
		}

		::remove(path);

		return parsed;
	}

	std::string ToString(const std::shared_ptr<const ov::Data> &data)
	{
		return (data == nullptr) ? std::string() : std::string(data->GetDataAs<char>(), data->GetLength());
	}

	// The parts a chunklist lists per segment, and the segments it lists as complete
	struct ChunklistState
	{
		int64_t last_part_segment = -1;
		std::map<int64_t, int64_t> part_counts;
		std::set<int64_t> completed_segments;
	};

	int64_t SegmentNumberOf(const std::string &uri)
	{
		// seg_<track>_<segment>_... and part_<track>_<segment>_<part>_...
		std::istringstream tokens(uri);
		std::string token;
		for (int index = 0; std::getline(tokens, token, '_'); index++)
		{
			if (index == 2)
			{
				return std::stoll(token);
			}
		}
		return -1;
	}

	ChunklistState ParseChunklist(const std::string &chunklist)
	{
		ChunklistState state;
		std::istringstream lines(chunklist);
		std::string line;
		while (std::getline(lines, line))
		{
			const std::string part_prefix = "#EXT-X-PART:";
			if (line.compare(0, part_prefix.size(), part_prefix) == 0)
			{
				auto uri_start = line.find("URI=\"");
				if (uri_start != std::string::npos)
				{
					state.last_part_segment = SegmentNumberOf(line.substr(uri_start + 5));
					state.part_counts[state.last_part_segment]++;
				}
			}
			else if (line.compare(0, 4, "seg_") == 0)
			{
				state.completed_segments.insert(SegmentNumberOf(line));
			}
		}
		return state;
	}
}  // namespace

// An origin-mode stream with one AAC track, fed frame by frame through the publisher's
// media path. The parameter selects server-time segment numbering.
class LLHlsSegmentTest : public testing::TestWithParam<bool>
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE(ParseVirtualHost(GetParam(), &vhost_config));
		host_info = std::make_shared<info::Host>("test-server", "test-server-id", vhost_config);
		app_info  = std::make_shared<TestApplicationInfo>(*host_info, vhost_config.GetApplicationList()[0]);
		app		  = std::make_shared<LLHlsApplication>(nullptr, *app_info);

		auto track = std::make_shared<MediaTrack>();
		track->SetId(kTrackId);
		track->SetMediaType(cmn::MediaType::Audio);
		track->SetCodecId(cmn::MediaCodecId::Aac);
		track->SetTimeBase(1, kSampleRate);
		track->SetSampleRate(kSampleRate);
		track->SetChannelLayout(cmn::AudioChannel::Layout::LayoutStereo);
		track->SetAudioSamplesPerFrame(kFrameSamples);
		auto audio_specific_config = std::make_shared<AudioSpecificConfig>();
		ASSERT_TRUE(audio_specific_config->Parse(std::make_shared<ov::Data>(kAudioSpecificConfig, sizeof(kAudioSpecificConfig))));
		track->SetDecoderConfigurationRecord(audio_specific_config);

		info::Stream stream_info(*app_info, StreamSourceType::Ovt);
		stream_info.SetName("stream");
		stream_info.AddTrack(track);
		// Like every publisher stream in production, it is linked to its input stream
		stream_info.LinkInputStream(std::make_shared<info::Stream>(*app_info, StreamSourceType::Ovt));

		stream = LLHlsStream::Create(app, stream_info, true, 0);
		ASSERT_TRUE(stream->EnterStart());
	}

	void TearDown() override
	{
		if (stream != nullptr)
		{
			stream->EnterStop(pub::Stream::StopCause::StreamDeleted);
		}
	}

	void SendFrame()
	{
		std::vector<uint8_t> payload(64, static_cast<uint8_t>(frame_index));
		auto dts = frame_index * kFrameSamples;
		stream->SendAudioFrame(std::make_shared<MediaPacket>(cmn::MediaType::Audio, kTrackId,
															 std::make_shared<ov::Data>(payload.data(), payload.size()),
															 dts, dts, kFrameSamples, MediaPacketFlag::Key,
															 cmn::BitstreamFormat::AAC_RAW, cmn::PacketType::RAW));
		frame_index++;
	}

	ChunklistState ReadChunklist() const
	{
		auto [result, chunklist] = stream->GetChunklist("", kTrackId, -1, -1, false, false, false, false);
		if (result != LLHlsStream::RequestResult::Success)
		{
			return {};
		}
		return ParseChunklist(ToString(chunklist));
	}

	// Feeds frames until the chunklist lists a part of a segment it does not list as
	// complete yet, and returns that segment's number
	int64_t FeedUntilSegmentInProgress()
	{
		for (int count = 0; count < kMaxFrames; count++)
		{
			SendFrame();
			auto state = ReadChunklist();
			if (state.last_part_segment >= 0 && state.completed_segments.count(state.last_part_segment) == 0)
			{
				// Server-time numbering counts 1 s slots since the epoch
				EXPECT_EQ(state.last_part_segment > 1000000000, GetParam());
				return state.last_part_segment;
			}
		}
		return -1;
	}

	// Feeds frames until the chunklist lists the segment as complete, and returns the
	// number of parts it lists for it
	int64_t FeedUntilSegmentCompleted(int64_t segment_number)
	{
		for (int count = 0; count < kMaxFrames; count++)
		{
			SendFrame();
			auto state = ReadChunklist();
			if (state.completed_segments.count(segment_number) > 0)
			{
				return state.part_counts[segment_number];
			}
		}
		return 0;
	}

	cfg::vhost::VirtualHost vhost_config;
	std::shared_ptr<info::Host> host_info;
	std::shared_ptr<TestApplicationInfo> app_info;
	std::shared_ptr<LLHlsApplication> app;
	std::shared_ptr<LLHlsStream> stream;
	int64_t frame_index = 0;
};

static std::string NumberingName(const testing::TestParamInfo<bool> &info)
{
	return info.param ? "ServerTime" : "FromZero";
}

class LLHlsSegmentRequest : public LLHlsSegmentTest
{
protected:
	std::string JoinedParts(int64_t segment_number, int64_t part_count) const
	{
		std::string joined;
		for (int64_t part = 0; part < part_count; part++)
		{
			auto [result, data] = stream->GetPartial(kTrackId, segment_number, part);
			EXPECT_EQ(result, LLHlsStream::RequestResult::Success);
			joined += ToString(data);
		}
		return joined;
	}
};

TEST_P(LLHlsSegmentRequest, InProgressSegmentIsNotFoundUntilCompleted)
{
	const auto segment_number = FeedUntilSegmentInProgress();
	ASSERT_GE(segment_number, 0);

	auto [in_progress, in_progress_data] = stream->GetSegment(kTrackId, segment_number);
	EXPECT_EQ(in_progress, LLHlsStream::RequestResult::NotFound);
	EXPECT_EQ(in_progress_data, nullptr);

	const auto part_count = FeedUntilSegmentCompleted(segment_number);
	ASSERT_GT(part_count, 0);

	auto [completed, completed_data] = stream->GetSegment(kTrackId, segment_number);
	EXPECT_EQ(completed, LLHlsStream::RequestResult::Success);
	ASSERT_NE(completed_data, nullptr);
	EXPECT_EQ(ToString(completed_data), JoinedParts(segment_number, part_count));
}

INSTANTIATE_TEST_SUITE_P(Numbering, LLHlsSegmentRequest, testing::Bool(), NumberingName);

namespace
{
	class TestRequest : public http::svr::h1::Http1Request
	{
	public:
		TestRequest(const ov::String &uri, const std::shared_ptr<ov::ClientSocket> &socket) : Http1Request(socket)
		{
			_request_uri = uri;
			_parsed_uri	 = ov::Url::Parse(uri);
		}
	};

	// Keeps what the HTTP/1 writer sends instead of writing it to the socket
	class TestResponse : public http::svr::h1::Http1Response
	{
	public:
		explicit TestResponse(const std::shared_ptr<ov::ClientSocket> &socket) : Http1Response(socket) {}

		std::string wire;

		std::string Head() const
		{
			return wire.substr(0, wire.find("\r\n\r\n") + 2);
		}

		std::string Body() const
		{
			auto end_of_head = wire.find("\r\n\r\n");
			return (end_of_head == std::string::npos) ? std::string() : wire.substr(end_of_head + 4);
		}

	private:
		bool Send(const void *data, size_t length) override
		{
			wire.append(static_cast<const char *>(data), length);
			return true;
		}

		bool Send(const std::shared_ptr<const ov::Data> &data) override
		{
			return Send(data->GetData(), data->GetLength());
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
			: HttpExchange(std::make_shared<TestConnection>(socket)),
			  request(std::make_shared<TestRequest>(uri, socket)),
			  response(std::make_shared<TestResponse>(socket))
		{
		}
		std::shared_ptr<http::svr::HttpRequest> GetRequest() const override { return request; }
		std::shared_ptr<http::svr::HttpResponse> GetResponse() const override { return response; }
		std::shared_ptr<TestRequest> request;
		std::shared_ptr<TestResponse> response;
	};

	// Loads a minimal Server.xml, since every HTTP response reads the server config
	bool LoadServerConfig()
	{
		char directory[] = "/tmp/ome_llhls_segment_test_XXXXXX";
		if (::mkdtemp(directory) == nullptr)
		{
			return false;
		}

		{
			std::ofstream out(std::filesystem::path(directory) / "Server.xml");
			out << "<Server version=\"8\">"
				<< "<Name>test</Name><Type>origin</Type><IP>*</IP><Bind></Bind>"
				<< "</Server>";
		}

		bool loaded = false;

		try
		{
			cfg::ConfigManager::GetInstance()->LoadConfigs(directory);
			loaded = (cfg::ConfigManager::GetInstance()->GetServer() != nullptr);
		}
		catch (const cfg::ConfigError &)
		{
			loaded = false;
		}

		std::filesystem::remove_all(directory);

		return loaded;
	}
}  // namespace

class LLHlsSegmentResponse : public LLHlsSegmentTest
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE(LoadServerConfig());
		// Satisfy transport invariants without listening or sending traffic.
		pool = ov::SocketPool::Create("llhls-segment-test", ov::SocketType::Tcp, false);
		ASSERT_TRUE(pool->Initialize(1));
		server_socket = pool->AllocSocket<ov::ServerSocket>(ov::SocketFamily::Inet, pool);
		ASSERT_NE(server_socket, nullptr);
		const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
		ASSERT_GE(fd, 0);
		client_socket = pool->AdoptSocket<ov::ClientSocket>(
			ov::SocketWrapper(ov::SocketType::Tcp, fd), server_socket, ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0));
		ASSERT_NE(client_socket, nullptr);

		LLHlsSegmentTest::SetUp();
		if (HasFatalFailure())
		{
			return;
		}

		session = stream->GetSessionFromPool();
		ASSERT_NE(session, nullptr);
	}

	void TearDown() override
	{
		session.reset();
		LLHlsSegmentTest::TearDown();
		if (client_socket) pool->ReleaseSocket(client_socket);
		if (server_socket) pool->ReleaseSocket(server_socket);
		if (pool) pool->Uninitialize();
	}

	std::shared_ptr<TestResponse> Request(const ov::String &file)
	{
		auto exchange = std::make_shared<TestExchange>(ov::String("http://localhost/app/stream/") + file, client_socket);
		session->OnMessageReceived(std::static_pointer_cast<http::svr::HttpExchange>(exchange));
		return exchange->response;
	}

	std::shared_ptr<TestResponse> RequestSegment(int64_t segment_number)
	{
		return Request(ov::String::FormatString("seg_%d_%" PRId64 "_audio_%s_llhls.m4s", kTrackId, segment_number, stream->GetStreamKey().CStr()));
	}

	std::shared_ptr<TestResponse> RequestPart(int64_t segment_number, int64_t part)
	{
		return Request(ov::String::FormatString("part_%d_%" PRId64 "_%" PRId64 "_audio_%s_llhls.m4s", kTrackId, segment_number, part, stream->GetStreamKey().CStr()));
	}

	std::shared_ptr<ov::SocketPool> pool;
	std::shared_ptr<ov::ServerSocket> server_socket;
	std::shared_ptr<ov::ClientSocket> client_socket;
	std::shared_ptr<LLHlsSession> session;
};

TEST_P(LLHlsSegmentResponse, InProgressSegmentIsNotFoundUntilCompleted)
{
	const auto segment_number = FeedUntilSegmentInProgress();
	ASSERT_GE(segment_number, 0);

	auto part = RequestPart(segment_number, 0);
	EXPECT_EQ(part->GetStatusCode(), http::StatusCode::OK);
	EXPECT_FALSE(part->Body().empty());

	auto in_progress = RequestSegment(segment_number);
	EXPECT_EQ(in_progress->GetStatusCode(), http::StatusCode::NotFound);
	EXPECT_EQ(in_progress->Body(), "");

	const auto part_count = FeedUntilSegmentCompleted(segment_number);
	ASSERT_GT(part_count, 0);

	auto completed = RequestSegment(segment_number);
	EXPECT_EQ(completed->GetStatusCode(), http::StatusCode::OK);

	std::string parts;
	for (int64_t index = 0; index < part_count; index++)
	{
		auto response = RequestPart(segment_number, index);
		EXPECT_EQ(response->GetStatusCode(), http::StatusCode::OK);
		parts += response->Body();
	}
	EXPECT_EQ(completed->Body(), parts);
}

TEST_P(LLHlsSegmentResponse, InProgressSegmentNotFoundIsNotCacheable)
{
	const auto segment_number = FeedUntilSegmentInProgress();
	ASSERT_GE(segment_number, 0);

	auto in_progress = RequestSegment(segment_number);
	EXPECT_EQ(in_progress->GetStatusCode(), http::StatusCode::NotFound);
	EXPECT_NE(in_progress->Head().find("\r\nCache-Control: no-cache, no-store\r\n"), std::string::npos) << in_progress->Head();
}

INSTANTIATE_TEST_SUITE_P(Numbering, LLHlsSegmentResponse, testing::Bool(), NumberingName);
