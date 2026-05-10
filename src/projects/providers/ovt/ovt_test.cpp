//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#define private public
#define protected public
#include <monitoring/monitoring.h>
#include <monitoring/stream_metrics.h>

#include "ovt_application.h"
#include "ovt_provider.h"
#include "ovt_stream.h"
#undef protected
#undef private

namespace
{
	class TestApplicationInfo final : public info::Application
	{
	public:
		TestApplicationInfo(const ov::String &vhost_name, const ov::String &app_name)
			: info::Application(
				  info::Host("test-server", "test-server-id", cfg::vhost::VirtualHost()),
				  1,
				  info::VHostAppName(vhost_name, app_name),
				  false)
		{
		}
	};

	struct NetworkProviderFixture
	{
		std::shared_ptr<pvd::OvtProvider> provider;
		std::shared_ptr<pvd::OvtApplication> application;
		std::shared_ptr<pvd::OvtStream> stream;
	};

	struct FakeOvtRequestRecord
	{
		ov::String application;
		ov::String target;
	};

	ov::String DumpRequestTargets(const std::vector<FakeOvtRequestRecord> &requests)
	{
		ov::String dump;
		for (size_t index = 0; index < requests.size(); ++index)
		{
			if (index > 0)
			{
				dump.Append(", ");
			}

			dump.AppendFormat("[%zu]=%s", index, requests[index].target.CStr());
		}

		return dump;
	}

	std::shared_ptr<MediaTrack> MakeTrack(uint32_t track_id,
										  cmn::MediaType media_type,
										  const ov::String &variant_name,
										  const ov::String &public_name);

	std::shared_ptr<const info::Playlist> MakePlaylist(const ov::String &name,
													   const ov::String &file_name,
													   const ov::String &video_variant_name,
													   int video_index_hint,
													   const ov::String &audio_variant_name,
													   int audio_index_hint);

	class FakeOvtControlServer final
	{
	public:
		using ResponseFactory = std::function<ov::String(uint32_t request_id, const ov::String &application)>;

		struct Step
		{
			ov::String expected_application;
			ov::String expected_target_path;
			ResponseFactory response_factory;
		};

		struct ConnectionPlan
		{
			std::vector<Step> steps;
			bool keep_open_after_plan = false;
		};

		explicit FakeOvtControlServer(std::vector<std::vector<Step>> connection_plans)
		{
			for (auto &connection_plan : connection_plans)
			{
				_connection_plans.push_back({std::move(connection_plan), false});
			}
		}

		explicit FakeOvtControlServer(std::vector<ConnectionPlan> connection_plans)
			: _connection_plans(std::move(connection_plans))
		{
		}

		~FakeOvtControlServer()
		{
			Stop();
		}

		bool Start()
		{
			_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
			if (_listen_fd < 0)
			{
				SetError("socket() failed");
				return false;
			}

			int reuse_addr = 1;
			(void)::setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

			sockaddr_in address{};
			address.sin_family		= AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port		= 0;

			if (::bind(_listen_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
			{
				SetError("bind() failed");
				::close(_listen_fd);
				_listen_fd = -1;
				return false;
			}

			if (::listen(_listen_fd, 4) != 0)
			{
				SetError("listen() failed");
				::close(_listen_fd);
				_listen_fd = -1;
				return false;
			}

			socklen_t address_length = sizeof(address);
			if (::getsockname(_listen_fd, reinterpret_cast<sockaddr *>(&address), &address_length) != 0)
			{
				SetError("getsockname() failed");
				::close(_listen_fd);
				_listen_fd = -1;
				return false;
			}

			_port		   = ntohs(address.sin_port);
			_server_thread = std::thread([this]() {
				Run();
			});
			return true;
		}

		void Stop()
		{
			_stop_requested = true;

			if (_listen_fd >= 0)
			{
				::shutdown(_listen_fd, SHUT_RDWR);
				::close(_listen_fd);
				_listen_fd = -1;
			}

			if (_server_thread.joinable())
			{
				_server_thread.join();
			}
		}

		uint16_t GetPort() const
		{
			return _port;
		}

		ov::String MakeUrl(const ov::String &path) const
		{
			return ov::String::FormatString("ovt://127.0.0.1:%u%s", _port, path.CStr());
		}

		std::vector<FakeOvtRequestRecord> GetRequests() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _requests;
		}

		ov::String GetError() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _error_message;
		}

	private:
		void Run()
		{
			for (const auto &connection_plan : _connection_plans)
			{
				sockaddr_in client_address{};
				socklen_t client_address_length = sizeof(client_address);
				int client_fd					= ::accept(_listen_fd, reinterpret_cast<sockaddr *>(&client_address), &client_address_length);
				if (client_fd < 0)
				{
					if (GetError().IsEmpty())
					{
						SetError("accept() failed");
					}
					return;
				}

				struct timeval timeout = {1, 0};
				(void)::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

				if (HandleConnection(client_fd, connection_plan) == false)
				{
					::shutdown(client_fd, SHUT_RDWR);
					::close(client_fd);
					return;
				}

				::shutdown(client_fd, SHUT_RDWR);
				::close(client_fd);
			}
		}

		bool HandleConnection(int client_fd, const ConnectionPlan &connection_plan)
		{
			OvtDepacketizer depacketizer;
			uint16_t sequence_number = 1;

			for (const auto &step : connection_plan.steps)
			{
				ov::String request_payload;
				if (ReadNextControlMessage(client_fd, depacketizer, request_payload) == false)
				{
					SetError("failed to read request payload");
					return false;
				}

				auto request_object = ov::Json::Parse(request_payload);
				if (request_object.IsNull())
				{
					SetError("invalid request json");
					return false;
				}

				auto &json_root		   = request_object.GetJsonValue();
				ov::String application = json_root["application"].asString().c_str();
				ov::String target	   = json_root["target"].asString().c_str();
				const auto request_id  = json_root["id"].asUInt();
				auto target_url		   = ov::Url::Parse(target.CStr());
				if (target_url == nullptr)
				{
					SetError("invalid request target url");
					return false;
				}

				{
					std::lock_guard<std::mutex> lock(_mutex);
					_requests.push_back({application, target});
				}

				if ((application != step.expected_application) || (target_url->Path() != step.expected_target_path))
				{
					SetError(ov::String::FormatString(
						"unexpected request: application=%s target=%s expected_application=%s expected_target_path=%s",
						application.CStr(),
						target.CStr(),
						step.expected_application.CStr(),
						step.expected_target_path.CStr()));
					return false;
				}

				if (SendResponse(client_fd, sequence_number++, step.response_factory(request_id, application)) == false)
				{
					SetError("failed to send response");
					return false;
				}
			}

			if (connection_plan.keep_open_after_plan)
			{
				char probe = 0;
				while (_stop_requested.load() == false)
				{
					auto peeked = ::recv(client_fd, &probe, sizeof(probe), MSG_PEEK);
					if (peeked == 0)
					{
						break;
					}

					if (peeked < 0)
					{
						if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
						{
							continue;
						}

						SetError("failed while waiting for client disconnect");
						return false;
					}
				}
			}

			return true;
		}

		bool ReadNextControlMessage(int client_fd, OvtDepacketizer &depacketizer, ov::String &payload)
		{
			payload = "";
			for (;;)
			{
				if (depacketizer.IsAvailableMessage())
				{
					auto message = depacketizer.PopMessage();
					if ((message == nullptr) || (message->GetLength() == 0))
					{
						return false;
					}

					payload = ov::String(message->GetDataAs<char>(), message->GetLength());
					return true;
				}

				uint8_t buffer[4096];
				const auto read_size = ::recv(client_fd, buffer, sizeof(buffer), 0);
				if (read_size <= 0)
				{
					return false;
				}

				if (depacketizer.AppendPacket(buffer, static_cast<size_t>(read_size)) == false)
				{
					return false;
				}
			}
		}

		bool SendResponse(int client_fd, uint16_t sequence_number, const ov::String &payload)
		{
			auto payload_data = payload.ToData(false);
			if (payload_data == nullptr)
			{
				return false;
			}

			OvtPacket packet;
			packet.SetMarker(true);
			packet.SetPayloadType(OVT_PAYLOAD_TYPE_MESSAGE_RESPONSE);
			packet.SetSequenceNumber(sequence_number);
			packet.SetTimestampNow();
			packet.SetSessionId(0);
			if (packet.SetPayload(payload_data->GetDataAs<uint8_t>(), payload_data->GetLength()) == false)
			{
				return false;
			}

			size_t offset	   = 0;
			const auto *buffer = packet.GetBuffer();
			const auto length  = packet.GetDataLength();
			while (offset < length)
			{
				const auto sent_size = ::send(client_fd, buffer + offset, length - offset, 0);
				if (sent_size <= 0)
				{
					return false;
				}
				offset += static_cast<size_t>(sent_size);
			}

			return true;
		}

		void SetError(const ov::String &message) const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_error_message.IsEmpty())
			{
				_error_message = message;
			}
		}

	private:
		std::vector<ConnectionPlan> _connection_plans;
		int _listen_fd = -1;
		uint16_t _port = 0;
		std::atomic<bool> _stop_requested{false};
		mutable std::mutex _mutex;
		mutable ov::String _error_message;
		std::vector<FakeOvtRequestRecord> _requests;
		std::thread _server_thread;
	};

	info::Stream MakeProviderStreamInfo(const std::shared_ptr<info::Application> &app_info)
	{
		info::Stream stream_info(*app_info, StreamSourceType::Ovt);
		stream_info.SetId(7);
		stream_info.SetName("stream");

		const std::vector<std::shared_ptr<MediaTrack>> tracks = {
			MakeTrack(101, cmn::MediaType::Video, "video-main", "video-main"),
			MakeTrack(102, cmn::MediaType::Audio, "audio-main", "audio-main"),
			MakeTrack(201, cmn::MediaType::Video, "video-mobile", "video-mobile"),
		};

		for (const auto &track : tracks)
		{
			if (stream_info.AddTrack(track) == false)
			{
				ADD_FAILURE() << "Failed to add track to provider stream fixture";
				return info::Stream(StreamSourceType::Ovt);
			}
		}

		const std::vector<std::shared_ptr<const info::Playlist>> playlists = {
			MakePlaylist("master", "master", "video-main", 0, "audio-main", 0),
			MakePlaylist("mobile", "mobile", "video-mobile", 0, "audio-main", 0),
		};

		for (const auto &playlist : playlists)
		{
			if (stream_info.AddPlaylist(playlist) == false)
			{
				ADD_FAILURE() << "Failed to add playlist to provider stream fixture";
				return info::Stream(StreamSourceType::Ovt);
			}
		}

		return stream_info;
	}

	std::shared_ptr<MediaTrack> MakeTrack(uint32_t track_id,
										  cmn::MediaType media_type,
										  const ov::String &variant_name,
										  const ov::String &public_name)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(track_id);
		track->SetMediaType(media_type);
		track->SetVariantName(variant_name);
		track->SetPublicName(public_name);
		track->SetLanguage("und");
		track->SetTimeBase(1, 1000);
		track->SetBitrateByConfig(1000000);
		track->SetStartFrameTime(0);
		track->SetLastFrameTime(1000);

		if (media_type == cmn::MediaType::Video)
		{
			track->SetCodecId(cmn::MediaCodecId::H264);
		}
		else
		{
			track->SetCodecId(cmn::MediaCodecId::Aac);
			track->SetSampleRate(48000);
		}

		return track;
	}

	std::shared_ptr<const info::Playlist> MakePlaylist(const ov::String &name,
													   const ov::String &file_name,
													   const ov::String &video_variant_name,
													   int video_index_hint,
													   const ov::String &audio_variant_name,
													   int audio_index_hint)
	{
		auto playlist  = std::make_shared<info::Playlist>(name, file_name, false);
		auto rendition = std::make_shared<info::Rendition>(name, video_variant_name, audio_variant_name);
		rendition->SetVideoIndexHint(video_index_hint);
		rendition->SetAudioIndexHint(audio_index_hint);
		playlist->AddRendition(rendition);
		return playlist;
	}

	std::shared_ptr<const ov::Url> ParseUrl(const char *url)
	{
		return ov::Url::Parse(url);
	}

	std::shared_ptr<pvd::OvtStream> MakeProviderOvtStream(const std::shared_ptr<info::Application> &app_info)
	{
		auto stream_info = MakeProviderStreamInfo(app_info);

		auto stream		 = std::make_shared<pvd::OvtStream>(
			nullptr,
			stream_info,
			std::vector<ov::String>{"ovt://origin/app/stream/mobile"},
			nullptr);
		stream->_inventory_snapshot_state = pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT;
		return stream;
	}

	NetworkProviderFixture MakeNetworkProviderFixture(const std::shared_ptr<info::Application> &app_info, const ov::String &start_url)
	{
		cfg::Server server_config;
		auto provider	 = std::make_shared<pvd::OvtProvider>(server_config, nullptr);
		auto application = std::make_shared<pvd::OvtApplication>(std::static_pointer_cast<pvd::PullProvider>(provider), *app_info);
		auto stream		 = std::make_shared<pvd::OvtStream>(
			application,
			MakeProviderStreamInfo(app_info),
			std::vector<ov::String>{start_url},
			nullptr);

		return {provider, application, stream};
	}

	ov::String MakeDescribeResponsePayload(std::optional<uint32_t> version, std::optional<bool> runtime_widening = true)
	{
		Json::Value root(Json::objectValue);
		Json::Value contents(Json::objectValue);
		Json::Value capabilities(Json::objectValue);
		Json::Value stream(Json::objectValue);
		Json::Value tracks(Json::arrayValue);
		Json::Value playlists(Json::arrayValue);

		if (version.has_value())
		{
			contents["version"] = *version;
		}

		if (runtime_widening.has_value())
		{
			capabilities["runtimeWidening"] = *runtime_widening;
			contents["capabilities"]		= capabilities;
		}

		stream["appName"]	 = "app";
		stream["streamName"] = "stream";

		Json::Value video_track(Json::objectValue);
		video_track["id"]						  = 101;
		video_track["name"]						  = "video-main";
		video_track["publicName"]				  = "video-main";
		video_track["language"]					  = "und";
		video_track["characteristics"]			  = "";
		video_track["codecId"]					  = static_cast<Json::UInt>(cmn::MediaCodecId::H264);
		video_track["mediaType"]				  = static_cast<Json::UInt>(cmn::MediaType::Video);
		video_track["timebaseNum"]				  = 1;
		video_track["timebaseDen"]				  = 1000;
		video_track["bitrate"]					  = 1000000;
		video_track["startFrameTime"]			  = Json::UInt64(0);
		video_track["lastFrameTime"]			  = Json::UInt64(1000);
		video_track["videoTrack"]["framerate"]	  = 30.0;
		video_track["videoTrack"]["maxFramerate"] = 30.0;
		video_track["videoTrack"]["width"]		  = 1280;
		video_track["videoTrack"]["height"]		  = 720;
		video_track["videoTrack"]["maxWidth"]	  = 1280;
		video_track["videoTrack"]["maxHeight"]	  = 720;
		tracks.append(video_track);

		Json::Value audio_track(Json::objectValue);
		audio_track["id"]						  = 102;
		audio_track["name"]						  = "audio-main";
		audio_track["publicName"]				  = "audio-main";
		audio_track["language"]					  = "und";
		audio_track["characteristics"]			  = "";
		audio_track["codecId"]					  = static_cast<Json::UInt>(cmn::MediaCodecId::Aac);
		audio_track["mediaType"]				  = static_cast<Json::UInt>(cmn::MediaType::Audio);
		audio_track["timebaseNum"]				  = 1;
		audio_track["timebaseDen"]				  = 1000;
		audio_track["bitrate"]					  = 128000;
		audio_track["startFrameTime"]			  = Json::UInt64(0);
		audio_track["lastFrameTime"]			  = Json::UInt64(1000);
		audio_track["audioTrack"]["samplerate"]	  = 48000;
		audio_track["audioTrack"]["sampleFormat"] = 0;
		audio_track["audioTrack"]["layout"]		  = 0;
		tracks.append(audio_track);

		Json::Value mobile_video_track(Json::objectValue);
		mobile_video_track["id"]						 = 201;
		mobile_video_track["name"]						 = "video-mobile";
		mobile_video_track["publicName"]				 = "video-mobile";
		mobile_video_track["language"]					 = "und";
		mobile_video_track["characteristics"]			 = "";
		mobile_video_track["codecId"]					 = static_cast<Json::UInt>(cmn::MediaCodecId::H264);
		mobile_video_track["mediaType"]					 = static_cast<Json::UInt>(cmn::MediaType::Video);
		mobile_video_track["timebaseNum"]				 = 1;
		mobile_video_track["timebaseDen"]				 = 1000;
		mobile_video_track["bitrate"]					 = 500000;
		mobile_video_track["startFrameTime"]			 = Json::UInt64(0);
		mobile_video_track["lastFrameTime"]				 = Json::UInt64(1000);
		mobile_video_track["videoTrack"]["framerate"]	 = 30.0;
		mobile_video_track["videoTrack"]["maxFramerate"] = 30.0;
		mobile_video_track["videoTrack"]["width"]		 = 640;
		mobile_video_track["videoTrack"]["height"]		 = 360;
		mobile_video_track["videoTrack"]["maxWidth"]	 = 640;
		mobile_video_track["videoTrack"]["maxHeight"]	 = 360;
		tracks.append(mobile_video_track);

		stream["tracks"] = tracks;

		Json::Value playlist(Json::objectValue);
		playlist["name"]	 = "master";
		playlist["fileName"] = "master";
		playlist["options"]	 = Json::Value(Json::objectValue);
		Json::Value rendition(Json::objectValue);
		rendition["name"]			= "master";
		rendition["videoTrackName"] = "video-main";
		rendition["audioTrackName"] = "audio-main";
		rendition["videoIndexHint"] = 0;
		rendition["audioIndexHint"] = 0;
		playlist["renditions"].append(rendition);
		playlists.append(playlist);

		Json::Value mobile_playlist(Json::objectValue);
		mobile_playlist["name"]		= "mobile";
		mobile_playlist["fileName"] = "mobile";
		mobile_playlist["options"]	= Json::Value(Json::objectValue);
		Json::Value mobile_rendition(Json::objectValue);
		mobile_rendition["name"]		   = "mobile";
		mobile_rendition["videoTrackName"] = "video-mobile";
		mobile_rendition["audioTrackName"] = "audio-main";
		mobile_rendition["videoIndexHint"] = 0;
		mobile_rendition["audioIndexHint"] = 0;
		mobile_playlist["renditions"].append(mobile_rendition);
		playlists.append(mobile_playlist);

		stream["playlists"] = playlists;
		contents["stream"]	= stream;
		root["contents"]	= contents;

		Json::StreamWriterBuilder builder;
		return Json::writeString(builder, root).c_str();
	}

	ov::String MakeControlResponsePayload(uint32_t request_id,
										  const ov::String &application,
										  uint32_t response_code,
										  const ov::String &response_message,
										  const ov::String &describe_payload = "")
	{
		Json::Value root(Json::objectValue);
		root["id"]			= request_id;
		root["application"] = application.CStr();
		root["code"]		= response_code;
		root["message"]		= response_message.CStr();

		if (describe_payload.IsEmpty() == false)
		{
			auto describe_object = ov::Json::Parse(describe_payload);
			root["contents"]	 = describe_object.GetJsonValue()["contents"];
		}

		Json::StreamWriterBuilder builder;
		return Json::writeString(builder, root).c_str();
	}

	template <typename Predicate>
	bool WaitUntil(Predicate predicate, int timeout_msec = 2000)
	{
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_msec);
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (predicate())
			{
				return true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		return predicate();
	}
}  // namespace

TEST(OvtProviderSharedRequestState, FullAndPlaylistDemandCoexistenceRequiresFullStreamTarget)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	EXPECT_FALSE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(stream->_shared_request_state.full_request_count, 0U);
	ASSERT_EQ(stream->_shared_request_state.playlist_request_counts.count("mobile"), 1U);
	EXPECT_EQ(stream->_shared_request_state.playlist_request_counts.at("mobile"), 1U);
	EXPECT_EQ(stream->_shared_request_state.resolved_target_track_ids, (std::set<int32_t>{102, 201}));
	EXPECT_FALSE(stream->_shared_request_state.runtime_widening_required);

	stream->RegisterDownstreamSession(
		2,
		ParseUrl("ovt://origin/app/stream"),
		ParseUrl("ovt://origin/app/stream"));

	EXPECT_TRUE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(stream->_shared_request_state.full_request_count, 1U);
	EXPECT_EQ(stream->_shared_request_state.resolved_target_track_ids, (std::set<int32_t>{101, 102, 201}));
	EXPECT_TRUE(stream->_shared_request_state.target_track_ids_are_authoritative);
	EXPECT_TRUE(stream->_shared_request_state.runtime_widening_required);

	stream->UnregisterDownstreamSession(2);
	EXPECT_FALSE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(stream->_shared_request_state.full_request_count, 0U);
	EXPECT_EQ(stream->_shared_request_state.resolved_target_track_ids, (std::set<int32_t>{102, 201}));
}

// B1 follow-up regression (issues.md re-review B1): a downstream session that has
// accepted a runtime-`subscribe` covering a *union* of multiple playlists must NOT be
// counted as a full-stream demand just because its decorative session URL has no file
// segment. The publisher signals the union via the explicit `authoritative_resolved_track_ids`
// argument; the provider must treat that set as authoritative for shared-request-state
// accumulation, leaving `full_request_count = 0` and populating `resolved_target_track_ids`
// from the explicit set.
TEST(OvtProviderSharedRequestState, AuthoritativeTrackSetSuppressesFullDemandMisclassification)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {101, 102, 201};

	// Register a session whose URL has no file segment (would naively be parsed as
	// "full stream") but whose authoritative track set is {102, 201} - i.e. a union of
	// two playlists, not the full track set {101, 102, 201}.
	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream"),
		ParseUrl("ovt://origin/app/stream"),
		std::optional<std::set<int32_t>>{{102, 201}});

	// Crucially: NOT counted as a full-stream demand. The provider used the explicit
	// track set instead of the URL.
	EXPECT_FALSE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(stream->_shared_request_state.full_request_count, 0U);
	// Track-level demand reflects the union exactly.
	EXPECT_EQ(stream->_shared_request_state.resolved_target_track_ids, (std::set<int32_t>{102, 201}));
	EXPECT_EQ(stream->_shared_request_state.track_ref_counts.size(), 2U);
	EXPECT_EQ(stream->_shared_request_state.track_ref_counts.at(102), 1U);
	EXPECT_EQ(stream->_shared_request_state.track_ref_counts.at(201), 1U);
	// No false `playlist_request_counts` entry because the URL had no file and the
	// union is anonymous (no single playlist name represents it).
	EXPECT_TRUE(stream->_shared_request_state.playlist_request_counts.empty());
	// Upstream already covers the union, so no widening should be required.
	EXPECT_FALSE(stream->_shared_request_state.runtime_widening_required);

	// A genuine full-stream session (no explicit track set, URL has no file) must
	// still be classified as a full demand - the new code path doesn't change the
	// existing URL-based fallback behaviour.
	stream->RegisterDownstreamSession(
		2,
		ParseUrl("ovt://origin/app/stream"),
		ParseUrl("ovt://origin/app/stream"));
	EXPECT_TRUE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(stream->_shared_request_state.full_request_count, 1U);

	// And a regular playlist-scoped session (with explicit track set hint) keeps the
	// playlist_request_counts entry alive so reconnect heuristics can still pick a
	// representative playlist URL for reprobe.
	stream->RegisterDownstreamSession(
		3,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"),
		std::optional<std::set<int32_t>>{{102, 201}});
	ASSERT_EQ(stream->_shared_request_state.playlist_request_counts.count("mobile"), 1U);
	EXPECT_EQ(stream->_shared_request_state.playlist_request_counts.at("mobile"), 1U);
}

TEST(OvtProviderSharedRequestState, UnsupportedPeerRequestsCompatibilityFallbackOnWidening)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_curr_url														 = ParseUrl("ovt://origin/app/stream/mobile");
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::UNSUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-master",
		ParseUrl("ovt://origin/app/stream/master"),
		ParseUrl("ovt://origin/app/stream/master"));

	EXPECT_EQ(stream->_shared_request_state.playlist_request_counts.size(), 2U);
	EXPECT_EQ(stream->_shared_request_state.resolved_target_track_ids, (std::set<int32_t>{101, 102, 201}));
	EXPECT_TRUE(stream->_shared_request_state.runtime_widening_required);
	EXPECT_TRUE(stream->_shared_request_state.compatibility_fallback_required);
	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_TRUE(stream->_compatibility_fallback_restart_pending);

	std::shared_ptr<const ov::Url> restart_target_url;
	ASSERT_TRUE(stream->ShouldTriggerCompatibilityFallbackRestart(&restart_target_url));
	ASSERT_NE(restart_target_url, nullptr);
	EXPECT_EQ(restart_target_url->Path(), "/app/stream");
	EXPECT_TRUE(restart_target_url->File().IsEmpty());
}

TEST(OvtProviderSharedRequestState, ReleaseInvalidatesInventoryAuthorityButKeepsActiveDemand)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	EXPECT_EQ(stream->_shared_request_state.inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT);
	EXPECT_TRUE(stream->_shared_request_state.target_track_ids_are_authoritative);
	EXPECT_FALSE(stream->_shared_request_state.target_computation_deferred);

	stream->Release();

	EXPECT_EQ(stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
	EXPECT_FALSE(stream->_current_upstream_subscription_state.is_known);
	EXPECT_EQ(stream->_shared_request_state.inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
	EXPECT_FALSE(stream->_shared_request_state.target_track_ids_are_authoritative);
	EXPECT_TRUE(stream->_shared_request_state.target_computation_deferred);
	ASSERT_EQ(stream->_shared_request_state.playlist_request_counts.count("mobile"), 1U);
	EXPECT_EQ(stream->_shared_request_state.playlist_request_counts.at("mobile"), 1U);
	EXPECT_FALSE(stream->_shared_request_state.runtime_widening_required);
	EXPECT_FALSE(stream->_shared_request_state.compatibility_fallback_required);
}

TEST(OvtProviderSharedRequestState, ReleaseKeepsFullDemandAndStickyCompatibilityModeForRecovery)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_compatibility_full_stream_mode_requested						 = true;
	stream->_compatibility_fallback_restart_pending							 = false;
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::UNSUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = true;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {101, 102, 201};
	stream->_runtime_full_describe_refresh_attempted_revision				 = 17;
	stream->_runtime_full_describe_refresh_in_flight_request_id				 = 18;
	stream->_runtime_subscribe_attempted_revision							 = 19;
	stream->_runtime_subscribe_in_flight_request_id							 = 20;

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-full",
		ParseUrl("ovt://origin/app/stream"),
		ParseUrl("ovt://origin/app/stream"));

	stream->Release();

	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
	EXPECT_EQ(stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNKNOWN);
	EXPECT_FALSE(stream->_current_upstream_subscription_state.is_known);
	EXPECT_EQ(stream->_runtime_full_describe_refresh_attempted_revision, 0U);
	EXPECT_EQ(stream->_runtime_full_describe_refresh_in_flight_request_id, 0U);
	EXPECT_EQ(stream->_runtime_subscribe_attempted_revision, 0U);
	EXPECT_EQ(stream->_runtime_subscribe_in_flight_request_id, 0U);
	EXPECT_EQ(stream->_shared_request_state.inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
	EXPECT_EQ(stream->_shared_request_state.full_request_count, 1U);
	EXPECT_TRUE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_TRUE(stream->_shared_request_state.target_computation_deferred);
	EXPECT_FALSE(stream->_shared_request_state.target_track_ids_are_authoritative);
	ASSERT_EQ(stream->_shared_request_state.playlist_request_counts.count("mobile"), 1U);
	EXPECT_EQ(stream->_shared_request_state.playlist_request_counts.at("mobile"), 1U);
	EXPECT_EQ(stream->_shared_request_state.resolved_target_track_ids, (std::set<int32_t>{101, 102, 201}));
	EXPECT_FALSE(stream->_shared_request_state.runtime_widening_required);
	EXPECT_FALSE(stream->_shared_request_state.compatibility_fallback_required);
}

TEST(OvtProviderSharedRequestState, BeginConnectionGenerationResetsTransientRuntimeStateOnly)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-master",
		ParseUrl("ovt://origin/app/stream/master"),
		ParseUrl("ovt://origin/app/stream/master"));

	stream->_last_request_id												 = 41;
	stream->_connection_generation											 = 9;
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::SUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {101, 102, 201};
	stream->_runtime_full_describe_refresh_attempted_revision				 = 77;
	stream->_runtime_full_describe_refresh_in_flight_request_id				 = 78;
	stream->_runtime_subscribe_attempted_revision							 = 79;
	stream->_runtime_subscribe_in_flight_request_id							 = 80;

	const auto previous_revision											 = stream->_active_request_state_revision;

	stream->BeginConnectionGeneration();

	EXPECT_EQ(stream->_connection_generation, 10U);
	EXPECT_EQ(stream->_current_generation_first_request_id, 42U);
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNKNOWN);
	EXPECT_FALSE(stream->_current_upstream_subscription_state.is_known);
	EXPECT_EQ(stream->_runtime_full_describe_refresh_attempted_revision, 0U);
	EXPECT_EQ(stream->_runtime_full_describe_refresh_in_flight_request_id, 0U);
	EXPECT_EQ(stream->_runtime_subscribe_attempted_revision, 0U);
	EXPECT_EQ(stream->_runtime_subscribe_in_flight_request_id, 0U);
	EXPECT_EQ(stream->_active_request_sessions.size(), 1U);
	EXPECT_EQ(stream->_active_request_scopes.size(), 1U);
	EXPECT_EQ(stream->_active_request_state_revision, previous_revision);
	ASSERT_EQ(stream->_shared_request_state.playlist_request_counts.count("mobile"), 1U);
	ASSERT_EQ(stream->_shared_request_state.playlist_request_counts.count("master"), 1U);
}

TEST(OvtProviderSharedRequestState, PrepareCompatibilityBootstrapRetryRewritesPlaylistUrlToFullStream)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_compatibility_fallback_restart_pending			  = true;
	stream->_shared_runtime_capability_state.runtime_widening = ovt::CapabilitySupport::SUPPORTED;

	std::shared_ptr<const ov::Url> retry_target_url;
	ASSERT_TRUE(stream->TryPrepareCompatibilityBootstrapRetry(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc"),
		retry_target_url));

	ASSERT_NE(retry_target_url, nullptr);
	EXPECT_EQ(retry_target_url->Scheme(), "ovt");
	EXPECT_EQ(retry_target_url->Host(), "origin.example.com");
	EXPECT_EQ(retry_target_url->Port(), 9000U);
	EXPECT_EQ(retry_target_url->Path(), "/app/stream");
	EXPECT_TRUE(retry_target_url->File().IsEmpty());
	EXPECT_EQ(retry_target_url->GetQueryValue("token"), "abc");
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNSUPPORTED);
	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
}

TEST(OvtProviderSharedRequestState, PrepareCompatibilityBootstrapRetryRejectsNonPlaylistScope)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	std::shared_ptr<const ov::Url> retry_target_url;
	EXPECT_FALSE(stream->TryPrepareCompatibilityBootstrapRetry(
		ParseUrl("ovt://origin/app/stream"),
		retry_target_url));
	EXPECT_EQ(retry_target_url, nullptr);
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNKNOWN);
	EXPECT_FALSE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
}

TEST(OvtProviderSharedRequestState, ProcessMediaFallbackDecisionConsumesPendingCompatibilityRestart)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_curr_url														 = ParseUrl("ovt://origin/app/stream/mobile");
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::UNSUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-master",
		ParseUrl("ovt://origin/app/stream/master"),
		ParseUrl("ovt://origin/app/stream/master"));

	ASSERT_TRUE(stream->_compatibility_fallback_restart_pending);

	std::shared_ptr<const ov::Url> restart_target_url;
	EXPECT_TRUE(stream->ShouldFinishProcessMediaForCompatibilityFallback(&restart_target_url));
	ASSERT_NE(restart_target_url, nullptr);
	EXPECT_EQ(restart_target_url->Path(), "/app/stream");
	EXPECT_TRUE(restart_target_url->File().IsEmpty());
}

TEST(OvtProviderSharedRequestState, ResolveRestartTargetUrlRewritesPlaylistScopeWhenCompatibilityModeIsSticky)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_compatibility_full_stream_mode_requested = true;

	auto restart_target								  = stream->ResolveRestartTargetUrl(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc"));
	ASSERT_NE(restart_target, nullptr);
	EXPECT_EQ(restart_target->Path(), "/app/stream");
	EXPECT_TRUE(restart_target->File().IsEmpty());
	EXPECT_EQ(restart_target->ToUrlString(), "ovt://origin.example.com:9000/app/stream?token=abc");
}

TEST(OvtProviderSharedRequestState, ResolveRestartTargetUrlPreservesPlaylistScopeWithoutCompatibilityMode)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_compatibility_full_stream_mode_requested = false;

	auto restart_target								  = stream->ResolveRestartTargetUrl(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc"));
	ASSERT_NE(restart_target, nullptr);
	EXPECT_EQ(restart_target->Path(), "/app/stream/mobile");
	EXPECT_EQ(restart_target->File(), "mobile");
	EXPECT_EQ(restart_target->ToUrlString(), "ovt://origin.example.com:9000/app/stream/mobile?token=abc");
}

TEST(OvtProviderSharedRequestState, NonOvtArtifactScopeIsNormalizedAsFullDemand)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("https://edge.example.com/app/stream/chunklist_video_llhls.m3u8?session=1_abc"),
		ParseUrl("https://edge.example.com/app/stream/chunklist_video_llhls.m3u8?session=1_abc"));

	EXPECT_EQ(stream->_shared_request_state.full_request_count, 1U);
	EXPECT_TRUE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_TRUE(stream->_shared_request_state.playlist_request_counts.empty());

	stream->UnregisterDownstreamSession(1);
	stream->RegisterDownstreamSession(
		2,
		ParseUrl("https://edge.example.com/app/stream/medialist_mobile_hls.m3u8"),
		ParseUrl("https://edge.example.com/app/stream/medialist_mobile_hls.m3u8"));

	EXPECT_EQ(stream->_shared_request_state.full_request_count, 1U);
	EXPECT_TRUE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_TRUE(stream->_shared_request_state.playlist_request_counts.empty());
}

TEST(OvtProviderSharedRequestState, PlaylistOnlyDemandSchedulesCompatibilityReprobeOnNextReconnect)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_compatibility_full_stream_mode_requested						 = true;
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::UNSUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = true;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {101, 102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_TRUE(stream->_compatibility_reprobe_on_next_reconnect);

	auto restart_target = stream->ResolveRestartTargetUrl(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc"));
	ASSERT_NE(restart_target, nullptr);
	EXPECT_EQ(restart_target->Path(), "/app/stream/mobile");
	EXPECT_EQ(restart_target->File(), "mobile");
	EXPECT_EQ(restart_target->ToUrlString(), "ovt://origin.example.com:9000/app/stream/mobile?token=abc");
}

TEST(OvtProviderSharedRequestState, PrePullCompatibilityModeSchedulesReprobeWithoutActiveDemand)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_compatibility_full_stream_mode_requested						 = true;
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::UNSUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = true;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {101, 102, 201};
	stream->_preferred_playlist_reprobe_file_name							 = "mobile";

	stream->RecalculateActiveRequestStateLocked();

	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_TRUE(stream->_compatibility_reprobe_on_next_reconnect);
	EXPECT_EQ(stream->_shared_request_state.full_request_count, 0U);
	EXPECT_TRUE(stream->_shared_request_state.playlist_request_counts.empty());

	auto restart_target = stream->ResolveRestartTargetUrl(
		ParseUrl("ovt://origin.example.com:9000/app/stream?token=abc"));
	ASSERT_NE(restart_target, nullptr);
	EXPECT_EQ(restart_target->Path(), "/app/stream/mobile");
	EXPECT_EQ(restart_target->File(), "mobile");
	EXPECT_EQ(restart_target->ToUrlString(), "ovt://origin.example.com:9000/app/stream/mobile?token=abc");
}

TEST(OvtProviderSharedRequestState, PlaylistScopedUpstreamClearsStickyCompatibilityModeAfterReprobe)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_compatibility_full_stream_mode_requested						 = true;
	stream->_compatibility_reprobe_on_next_reconnect						 = true;
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::UNSUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	EXPECT_FALSE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(stream->_compatibility_reprobe_on_next_reconnect);

	auto restart_target = stream->ResolveRestartTargetUrl(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc"));
	ASSERT_NE(restart_target, nullptr);
	EXPECT_EQ(restart_target->Path(), "/app/stream/mobile");
	EXPECT_EQ(restart_target->File(), "mobile");
}

TEST(OvtProviderSharedRequestState, RemovingAllDemandClearsPendingCompatibilityRestart)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_curr_url														 = ParseUrl("ovt://origin/app/stream/mobile");
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::UNSUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-master",
		ParseUrl("ovt://origin/app/stream/master"),
		ParseUrl("ovt://origin/app/stream/master"));

	ASSERT_TRUE(stream->_compatibility_fallback_restart_pending);
	ASSERT_TRUE(stream->_shared_request_state.compatibility_fallback_required);

	stream->UnregisterDownstreamRequest("req-master");
	EXPECT_TRUE(stream->_compatibility_fallback_restart_pending);
	EXPECT_TRUE(stream->ShouldFinishProcessMediaForCompatibilityFallback());

	stream->UnregisterDownstreamSession(1);

	EXPECT_FALSE(stream->_shared_request_state.compatibility_fallback_required);
	EXPECT_FALSE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
	std::shared_ptr<const ov::Url> restart_target_url;
	EXPECT_FALSE(stream->ShouldFinishProcessMediaForCompatibilityFallback(&restart_target_url));
	EXPECT_EQ(restart_target_url, nullptr);
}

TEST(OvtProviderSharedRequestState, FinalizeCompatibilityBootstrapRetryPreparationResetsConnectionStateButKeepsCompatibilityMode)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->SetState(pvd::Stream::State::PLAYING);
	stream->_curr_url														 = ParseUrl("ovt://origin/app/stream/mobile");
	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::SUPPORTED;
	stream->_compatibility_full_stream_mode_requested						 = true;
	stream->_compatibility_fallback_restart_pending							 = true;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	stream->FinalizeCompatibilityBootstrapRetryPreparation();

	EXPECT_EQ(stream->GetState(), pvd::Stream::State::IDLE);
	EXPECT_EQ(stream->_curr_url, nullptr);
	EXPECT_EQ(stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNSUPPORTED);
	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
	EXPECT_FALSE(stream->_current_upstream_subscription_state.is_known);
	EXPECT_EQ(stream->_shared_request_state.inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
	ASSERT_EQ(stream->_shared_request_state.playlist_request_counts.count("mobile"), 1U);
	EXPECT_TRUE(stream->_shared_request_state.target_computation_deferred);
	EXPECT_FALSE(stream->_shared_request_state.target_track_ids_are_authoritative);
}

TEST(OvtProviderSharedRequestState, RetryBootstrapInCompatibilityModeInvokesStartStreamWithFullStreamTarget)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->SetState(pvd::Stream::State::PLAYING);
	stream->_curr_url										  = ParseUrl("ovt://origin/app/stream/mobile?token=abc");
	stream->_shared_runtime_capability_state.runtime_widening = ovt::CapabilitySupport::SUPPORTED;
	stream->_compatibility_full_stream_mode_requested		  = false;
	stream->_compatibility_fallback_restart_pending			  = true;
	stream->_last_request_id								  = 41;
	stream->_connection_generation							  = 9;

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	int invoked_count = 0;
	std::shared_ptr<const ov::Url> invoked_target_url;
	stream->SetStartStreamInvokerForTest([&](const std::shared_ptr<const ov::Url> &url) {
		++invoked_count;
		invoked_target_url = url;
		return true;
	});

	EXPECT_TRUE(stream->RetryBootstrapInCompatibilityMode(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc")));

	EXPECT_EQ(invoked_count, 1);
	ASSERT_NE(invoked_target_url, nullptr);
	EXPECT_EQ(invoked_target_url->Path(), "/app/stream");
	EXPECT_TRUE(invoked_target_url->File().IsEmpty());
	EXPECT_EQ(invoked_target_url->ToUrlString(), "ovt://origin.example.com:9000/app/stream?token=abc");
	EXPECT_EQ(stream->GetState(), pvd::Stream::State::IDLE);
	EXPECT_EQ(stream->_curr_url, nullptr);
	EXPECT_EQ(stream->_connection_generation, 10U);
	EXPECT_EQ(stream->_current_generation_first_request_id, 42U);
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNSUPPORTED);
	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
	EXPECT_EQ(stream->_shared_request_state.inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
}

TEST(OvtProviderSharedRequestState, RestartStreamUsesPlaylistReprobeTargetAndClearsPendingFlag)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_inventory_snapshot_state				  = pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT;
	stream->_compatibility_full_stream_mode_requested = true;
	stream->_compatibility_fallback_restart_pending	  = true;
	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	int invoked_count = 0;
	std::shared_ptr<const ov::Url> invoked_target_url;
	stream->SetStartStreamInvokerForTest([&](const std::shared_ptr<const ov::Url> &url) {
		++invoked_count;
		invoked_target_url = url;
		return true;
	});

	EXPECT_TRUE(stream->RestartStream(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc")));

	EXPECT_EQ(invoked_count, 1);
	ASSERT_NE(invoked_target_url, nullptr);
	EXPECT_EQ(invoked_target_url->Path(), "/app/stream/mobile");
	EXPECT_EQ(invoked_target_url->File(), "mobile");
	EXPECT_EQ(invoked_target_url->ToUrlString(), "ovt://origin.example.com:9000/app/stream/mobile?token=abc");
	EXPECT_EQ(stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_TRUE(stream->_compatibility_reprobe_on_next_reconnect);
}

TEST(OvtProviderSharedRequestState, CompatibilityBootstrapRetryRemembersPlaylistReprobeScope)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	int invoked_count = 0;
	std::shared_ptr<const ov::Url> invoked_target_url;
	stream->SetStartStreamInvokerForTest([&](const std::shared_ptr<const ov::Url> &url) {
		++invoked_count;
		invoked_target_url = url;
		return true;
	});

	EXPECT_TRUE(stream->RetryBootstrapInCompatibilityMode(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc")));

	EXPECT_EQ(invoked_count, 1);
	ASSERT_NE(invoked_target_url, nullptr);
	EXPECT_EQ(invoked_target_url->Path(), "/app/stream");
	EXPECT_EQ(stream->_preferred_playlist_reprobe_file_name, "mobile");

	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = true;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {101, 102, 201};
	stream->RecalculateActiveRequestStateLocked();

	EXPECT_TRUE(stream->_compatibility_reprobe_on_next_reconnect);

	auto restart_target = stream->ResolveRestartTargetUrl(
		ParseUrl("ovt://origin.example.com:9000/app/stream?token=abc"));
	ASSERT_NE(restart_target, nullptr);
	EXPECT_EQ(restart_target->Path(), "/app/stream/mobile");
	EXPECT_EQ(restart_target->File(), "mobile");
}

TEST(OvtProviderSharedRequestState, RestartStreamPreservesPlaylistScopeWithoutCompatibilityMode)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_inventory_snapshot_state				  = pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT;
	stream->_compatibility_full_stream_mode_requested = false;
	stream->_compatibility_fallback_restart_pending	  = false;

	int invoked_count								  = 0;
	std::shared_ptr<const ov::Url> invoked_target_url;
	stream->SetStartStreamInvokerForTest([&](const std::shared_ptr<const ov::Url> &url) {
		++invoked_count;
		invoked_target_url = url;
		return true;
	});

	EXPECT_TRUE(stream->RestartStream(
		ParseUrl("ovt://origin.example.com:9000/app/stream/mobile?token=abc")));

	EXPECT_EQ(invoked_count, 1);
	ASSERT_NE(invoked_target_url, nullptr);
	EXPECT_EQ(invoked_target_url->Path(), "/app/stream/mobile");
	EXPECT_EQ(invoked_target_url->File(), "mobile");
	EXPECT_EQ(invoked_target_url->ToUrlString(), "ovt://origin.example.com:9000/app/stream/mobile?token=abc");
	EXPECT_EQ(stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::UNKNOWN);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
	EXPECT_FALSE(stream->_compatibility_full_stream_mode_requested);
}

TEST(OvtProviderSharedRequestState, HandleSubscribeResponseAppliesFullStreamTargetAndClearsWideningNeed)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::SUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-full",
		ParseUrl("ovt://origin/app/stream"),
		ParseUrl("ovt://origin/app/stream"));

	ASSERT_TRUE(stream->_shared_request_state.runtime_widening_required);

	EXPECT_TRUE(stream->HandleSubscribeResponseForTest(
		"SUBSCRIBE",
		"subscribe",
		200,
		true,
		std::nullopt));
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::SUPPORTED);
	EXPECT_TRUE(stream->_current_upstream_subscription_state.is_known);
	EXPECT_TRUE(stream->_current_upstream_subscription_state.is_full_stream);
	EXPECT_TRUE(stream->_current_upstream_subscription_state.track_ids_are_authoritative);
	EXPECT_TRUE(stream->_current_upstream_subscription_state.resolved_track_ids.empty());
	EXPECT_FALSE(stream->_shared_request_state.runtime_widening_required);
	EXPECT_FALSE(stream->_shared_request_state.compatibility_fallback_required);
	EXPECT_TRUE(stream->_shared_request_state.target_requires_full_stream);
}

// Spec section 4.4: only 400 (selection violates protocol) and 404 (unknown track id)
// indicate a real capability gap and warrant downgrading to UNSUPPORTED. 5xx responses
// represent transient origin failures and must NOT downgrade -- otherwise a single
// origin restart permanently disables runtime widening on the connection.
TEST(OvtProviderSharedRequestState, HandleSubscribeResponse400DowngradesCapabilityAndKeepsFallbackGate)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::SUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-full",
		ParseUrl("ovt://origin/app/stream"),
		ParseUrl("ovt://origin/app/stream"));

	EXPECT_FALSE(stream->HandleSubscribeResponseForTest(
		"SUBSCRIBE",
		"subscribe",
		400,
		true,
		std::nullopt));
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNSUPPORTED);
	EXPECT_TRUE(stream->_shared_request_state.runtime_widening_required);
	EXPECT_TRUE(stream->_shared_request_state.compatibility_fallback_required);
	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_TRUE(stream->_compatibility_fallback_restart_pending);
	EXPECT_FALSE(stream->_current_upstream_subscription_state.is_full_stream);
}

TEST(OvtProviderSharedRequestState, HandleSubscribeResponse5xxLeavesCapabilityIntactForRetry)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::SUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-full",
		ParseUrl("ovt://origin/app/stream"),
		ParseUrl("ovt://origin/app/stream"));

	// Pretend a previous subscribe was sent at revision R so that, without B2's explicit
	// reset, _attempted_revision would still equal _active_request_state_revision after
	// the 5xx response and gate the next retry. (RecalculateActiveRequestStateLocked
	// happens to bump revision on every call, but B2 requires us not to depend on that
	// incidental side-effect - see issues.md B2 / requirements.md section 4.8.)
	stream->_runtime_subscribe_attempted_revision = stream->_active_request_state_revision;
	const auto revision_before					  = stream->_active_request_state_revision;

	EXPECT_FALSE(stream->HandleSubscribeResponseForTest(
		"SUBSCRIBE",
		"subscribe",
		503,
		true,
		std::nullopt));
	// Capability MUST stay SUPPORTED so the next revision change can retry the subscribe.
	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::SUPPORTED);
	// No fallback should be triggered for a transient origin failure.
	EXPECT_FALSE(stream->_shared_request_state.compatibility_fallback_required);
	EXPECT_FALSE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(stream->_compatibility_fallback_restart_pending);
	// _attempted_revision MUST be reset explicitly to 0 (mirroring the timeout path), so
	// that the retry gate opens regardless of whether Recalc happens to bump the revision.
	EXPECT_EQ(stream->_runtime_subscribe_attempted_revision, 0u);
	// Sanity: revision DID move (Recalc still runs) but that's not what unblocks retry.
	EXPECT_GT(stream->_active_request_state_revision, revision_before);
}

TEST(OvtProviderSharedRequestState, HandleDescribeResponseVersionMismatchDowngradesCapabilityAndTriggersFallbackGate)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->_shared_runtime_capability_state.runtime_widening				 = ovt::CapabilitySupport::SUPPORTED;
	stream->_current_upstream_subscription_state.is_known					 = true;
	stream->_current_upstream_subscription_state.is_full_stream				 = false;
	stream->_current_upstream_subscription_state.track_ids_are_authoritative = true;
	stream->_current_upstream_subscription_state.resolved_track_ids			 = {102, 201};

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));
	stream->RegisterDownstreamRequest(
		"req-full",
		ParseUrl("ovt://origin/app/stream"),
		ParseUrl("ovt://origin/app/stream"));

	ASSERT_TRUE(stream->_shared_request_state.runtime_widening_required);
	EXPECT_FALSE(stream->_shared_request_state.compatibility_fallback_required);

	EXPECT_TRUE(stream->HandleDescribeResponseForTest(
		"DESCRIBE",
		"describe",
		200,
		MakeDescribeResponsePayload(OVT_SIGNALING_VERSION + 1),
		pvd::OvtStream::InventoryUpdateMode::REPLACE_ALL));

	EXPECT_EQ(stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNSUPPORTED);
	EXPECT_TRUE(stream->_shared_request_state.runtime_widening_required);
	EXPECT_TRUE(stream->_shared_request_state.compatibility_fallback_required);
	EXPECT_TRUE(stream->_compatibility_full_stream_mode_requested);
	EXPECT_TRUE(stream->_compatibility_fallback_restart_pending);
	EXPECT_TRUE(stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(stream->_shared_request_state.inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT);
}

TEST(OvtProviderSharedRequestState, RuntimeFullDescribeRefreshRejectReopensSameRevisionRetry)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	stream->SetState(pvd::Stream::State::PLAYING);
	stream->_curr_url				  = ParseUrl("ovt://origin/app/stream/mobile");
	stream->_inventory_snapshot_state = pvd::OvtStream::InventorySnapshotState::UNKNOWN;

	stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	ASSERT_TRUE(stream->_shared_request_state.target_computation_deferred);
	ASSERT_FALSE(stream->_shared_request_state.target_track_ids_are_authoritative);

	const auto current_revision									= stream->_active_request_state_revision;
	stream->_runtime_full_describe_refresh_attempted_revision	= current_revision;
	stream->_runtime_full_describe_refresh_in_flight_request_id = 55;
	stream->_runtime_full_describe_refresh_request_time_msec	= ov::Clock::NowMSec();

	ASSERT_TRUE(stream->QueueCompletedRuntimeDescribeRefreshForTest(
		55,
		ParseUrl("ovt://origin/app/stream"),
		503,
		"retryable reject"));

	stream->ProcessCompletedRuntimeControlRequests();

	EXPECT_EQ(stream->_runtime_full_describe_refresh_attempted_revision, 0U);
	EXPECT_EQ(stream->_runtime_full_describe_refresh_in_flight_request_id, 0U);
	EXPECT_TRUE(stream->ShouldRequestRuntimeFullDescribeRefreshLocked());
}

TEST(OvtProviderSharedRequestState, RuntimeFullDescribeRefreshSendFailureReopensSameRevisionRetry)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto fixture  = MakeNetworkProviderFixture(app_info, "ovt://127.0.0.1:1/app/stream/mobile");
	ASSERT_NE(fixture.provider, nullptr);
	ASSERT_NE(fixture.stream, nullptr);

	fixture.stream->SetState(pvd::Stream::State::PLAYING);
	fixture.stream->_curr_url	   = ParseUrl("ovt://origin/app/stream/mobile");
	fixture.stream->_client_socket = fixture.provider->GetClientSocketPool()->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(fixture.stream->_client_socket, nullptr);
	fixture.stream->_inventory_snapshot_state = pvd::OvtStream::InventorySnapshotState::UNKNOWN;

	fixture.stream->RegisterDownstreamSession(
		1,
		ParseUrl("ovt://origin/app/stream/mobile"),
		ParseUrl("ovt://origin/app/stream/mobile"));

	ASSERT_TRUE(fixture.stream->_shared_request_state.target_computation_deferred);
	ASSERT_FALSE(fixture.stream->_shared_request_state.target_track_ids_are_authoritative);

	fixture.stream->MaybeRequestRuntimeFullDescribeRefresh();

	EXPECT_EQ(fixture.stream->_runtime_full_describe_refresh_attempted_revision, 0U);
	EXPECT_EQ(fixture.stream->_runtime_full_describe_refresh_in_flight_request_id, 0U);
	EXPECT_TRUE(fixture.stream->_pending_control_requests.empty());
	EXPECT_TRUE(fixture.stream->ShouldRequestRuntimeFullDescribeRefreshLocked());
}

TEST(OvtProviderSharedRequestState, DuplicateCompletedControlResponseIsIgnored)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	auto pending_request = stream->CreatePendingControlRequest("describe", 91);
	ASSERT_NE(pending_request, nullptr);
	ASSERT_TRUE(stream->CompletePendingControlResponse(91, "describe", 200, "ok", MakeDescribeResponsePayload(OVT_SIGNALING_VERSION)));

	auto completed_request = stream->TakePendingControlRequest(91);
	ASSERT_NE(completed_request, nullptr);
	EXPECT_TRUE(stream->WasRecentlyCompletedControlRequestId(91));

	auto duplicate_payload = MakeControlResponsePayload(91, "describe", 200, "ok", MakeDescribeResponsePayload(OVT_SIGNALING_VERSION));
	auto duplicate_message = duplicate_payload.ToData(false);
	ASSERT_NE(duplicate_message, nullptr);

	EXPECT_TRUE(stream->DispatchControlMessageLocked(duplicate_message));
}

TEST(OvtProviderSharedRequestState, DuplicateCompletedControlResponseIsIgnoredBeforeTakePendingRequest)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeProviderOvtStream(app_info);
	ASSERT_NE(stream, nullptr);

	auto pending_request = stream->CreatePendingControlRequest("describe", 92);
	ASSERT_NE(pending_request, nullptr);
	ASSERT_TRUE(stream->CompletePendingControlResponse(92, "describe", 200, "ok", MakeDescribeResponsePayload(OVT_SIGNALING_VERSION)));

	auto duplicate_payload = MakeControlResponsePayload(92, "describe", 200, "ok", MakeDescribeResponsePayload(OVT_SIGNALING_VERSION));
	auto duplicate_message = duplicate_payload.ToData(false);
	ASSERT_NE(duplicate_message, nullptr);

	EXPECT_TRUE(stream->DispatchControlMessageLocked(duplicate_message));
	EXPECT_TRUE(stream->WasRecentlyCompletedControlRequestId(92));
}

TEST(OvtProviderNetwork, StartStreamRetriesInCompatibilityModeAfterBootstrapRefreshFailure)
{
	auto app_info							  = std::make_shared<TestApplicationInfo>("default", "app");
	const auto initial_describe_payload		  = MakeDescribeResponsePayload(OVT_SIGNALING_VERSION, true);
	const auto compatibility_describe_payload = MakeDescribeResponsePayload(std::nullopt, std::nullopt);

	FakeOvtControlServer server({
		{
			{
				"describe",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", initial_describe_payload);
				},
			},
			{
				"play",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 503, "bootstrap refresh failed");
				},
			},
		},
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", compatibility_describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
	});
	ASSERT_TRUE(server.Start());

	const auto playlist_url	   = server.MakeUrl("/app/stream/mobile");
	const auto full_stream_url = server.MakeUrl("/app/stream");

	auto fixture			   = MakeNetworkProviderFixture(app_info, playlist_url);
	ASSERT_NE(fixture.stream, nullptr);
	fixture.stream->RegisterDownstreamSession(1, ParseUrl(playlist_url.CStr()), ParseUrl(playlist_url.CStr()));

	EXPECT_TRUE(fixture.stream->StartStream(ParseUrl(playlist_url.CStr())));
	ASSERT_EQ(server.GetError().IsEmpty(), true) << server.GetError().CStr();

	ASSERT_NE(fixture.stream->_curr_url, nullptr);
	EXPECT_EQ(fixture.stream->GetState(), pvd::Stream::State::PLAYING);
	EXPECT_EQ(fixture.stream->_curr_url->Path(), "/app/stream");
	EXPECT_TRUE(fixture.stream->_curr_url->File().IsEmpty());
	EXPECT_TRUE(fixture.stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(fixture.stream->_compatibility_fallback_restart_pending);
	EXPECT_EQ(fixture.stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNSUPPORTED);
	EXPECT_TRUE(fixture.stream->_current_upstream_subscription_state.is_known);
	EXPECT_TRUE(fixture.stream->_current_upstream_subscription_state.is_full_stream);
	EXPECT_EQ(fixture.stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT);

	const auto requests = server.GetRequests();
	ASSERT_EQ(requests.size(), 5U);
	EXPECT_EQ(requests[0].application, "describe");
	EXPECT_EQ(requests[0].target, playlist_url);
	EXPECT_EQ(requests[1].application, "play");
	EXPECT_EQ(requests[1].target, playlist_url);
	EXPECT_EQ(requests[2].application, "describe");
	EXPECT_EQ(requests[2].target, full_stream_url);
	EXPECT_EQ(requests[3].application, "describe");
	EXPECT_EQ(requests[3].target, full_stream_url);
	EXPECT_EQ(requests[4].application, "play");
	EXPECT_EQ(requests[4].target, full_stream_url);

	fixture.stream->Stop();
	server.Stop();
}

TEST(OvtProviderNetwork, StartStreamKeepsPlaylistBootstrapButDisablesRuntimeWideningForVersionMismatchPeer)
{
	auto app_info							  = std::make_shared<TestApplicationInfo>("default", "app");
	const auto mixed_version_describe_payload = MakeDescribeResponsePayload(OVT_SIGNALING_VERSION + 1);

	FakeOvtControlServer server({
		{
			{
				"describe",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", mixed_version_describe_payload);
				},
			},
			{
				"play",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", mixed_version_describe_payload);
				},
			},
		},
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", mixed_version_describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
	});
	ASSERT_TRUE(server.Start());

	const auto playlist_url	   = server.MakeUrl("/app/stream/mobile");
	const auto full_stream_url = server.MakeUrl("/app/stream");

	auto fixture			   = MakeNetworkProviderFixture(app_info, playlist_url);
	ASSERT_NE(fixture.stream, nullptr);
	fixture.stream->RegisterDownstreamSession(1, ParseUrl(playlist_url.CStr()), ParseUrl(playlist_url.CStr()));

	EXPECT_TRUE(fixture.stream->StartStream(ParseUrl(playlist_url.CStr())));
	ASSERT_EQ(server.GetError().IsEmpty(), true) << server.GetError().CStr();

	ASSERT_NE(fixture.stream->_curr_url, nullptr);
	EXPECT_EQ(fixture.stream->GetState(), pvd::Stream::State::PLAYING);
	EXPECT_EQ(fixture.stream->_curr_url->Path(), "/app/stream/mobile");
	EXPECT_EQ(fixture.stream->_curr_url->File(), "mobile");
	EXPECT_EQ(fixture.stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNSUPPORTED);
	EXPECT_FALSE(fixture.stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(fixture.stream->_compatibility_fallback_restart_pending);
	EXPECT_EQ(fixture.stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT);
	EXPECT_TRUE(fixture.stream->_current_upstream_subscription_state.is_known);
	EXPECT_FALSE(fixture.stream->_current_upstream_subscription_state.is_full_stream);

	const auto requests = server.GetRequests();
	ASSERT_EQ(requests.size(), 3U);
	EXPECT_EQ(requests[0].application, "describe");
	EXPECT_EQ(requests[0].target, playlist_url);
	EXPECT_EQ(requests[1].application, "play");
	EXPECT_EQ(requests[1].target, playlist_url);
	EXPECT_EQ(requests[2].application, "describe");
	EXPECT_EQ(requests[2].target, full_stream_url);

	fixture.stream->Stop();
	server.Stop();
}

TEST(OvtProviderNetwork, RestartStreamRetriesCompatibilityFallbackAfterPlaylistReprobeFailure)
{
	auto app_info							  = std::make_shared<TestApplicationInfo>("default", "app");
	const auto initial_describe_payload		  = MakeDescribeResponsePayload(OVT_SIGNALING_VERSION, true);
	const auto compatibility_describe_payload = MakeDescribeResponsePayload(std::nullopt, std::nullopt);

	FakeOvtControlServer server({
		{
			{
				"describe",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", initial_describe_payload);
				},
			},
			{
				"play",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 503, "re-probe refresh failed");
				},
			},
		},
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", compatibility_describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
	});
	ASSERT_TRUE(server.Start());

	const auto playlist_url	   = server.MakeUrl("/app/stream/mobile");
	const auto full_stream_url = server.MakeUrl("/app/stream");

	auto fixture			   = MakeNetworkProviderFixture(app_info, playlist_url);
	ASSERT_NE(fixture.stream, nullptr);
	fixture.stream->_compatibility_full_stream_mode_requested = true;
	fixture.stream->_compatibility_reprobe_on_next_reconnect  = true;
	fixture.stream->RegisterDownstreamSession(1, ParseUrl(playlist_url.CStr()), ParseUrl(playlist_url.CStr()));

	EXPECT_TRUE(fixture.stream->RestartStream(ParseUrl(playlist_url.CStr())));
	ASSERT_EQ(server.GetError().IsEmpty(), true) << server.GetError().CStr();

	ASSERT_NE(fixture.stream->_curr_url, nullptr);
	EXPECT_EQ(fixture.stream->GetState(), pvd::Stream::State::PLAYING);
	EXPECT_EQ(fixture.stream->_curr_url->Path(), "/app/stream");
	EXPECT_TRUE(fixture.stream->_curr_url->File().IsEmpty());
	EXPECT_TRUE(fixture.stream->_compatibility_full_stream_mode_requested);
	EXPECT_TRUE(fixture.stream->_compatibility_reprobe_on_next_reconnect);
	EXPECT_FALSE(fixture.stream->_compatibility_fallback_restart_pending);
	EXPECT_EQ(fixture.stream->_shared_runtime_capability_state.runtime_widening, ovt::CapabilitySupport::UNSUPPORTED);
	EXPECT_TRUE(fixture.stream->_current_upstream_subscription_state.is_known);
	EXPECT_TRUE(fixture.stream->_current_upstream_subscription_state.is_full_stream);
	EXPECT_EQ(fixture.stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT);

	const auto requests = server.GetRequests();
	ASSERT_EQ(requests.size(), 5U);
	EXPECT_EQ(requests[0].application, "describe");
	EXPECT_EQ(requests[0].target, playlist_url);
	EXPECT_EQ(requests[1].application, "play");
	EXPECT_EQ(requests[1].target, playlist_url);
	EXPECT_EQ(requests[2].application, "describe");
	EXPECT_EQ(requests[2].target, full_stream_url);
	EXPECT_EQ(requests[3].application, "describe");
	EXPECT_EQ(requests[3].target, full_stream_url);
	EXPECT_EQ(requests[4].application, "play");
	EXPECT_EQ(requests[4].target, full_stream_url);

	fixture.stream->Stop();
	server.Stop();
}

TEST(OvtProviderNetwork, RestartStreamUsesPlaylistReprobeTargetOverSocket)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");

	FakeOvtControlServer server({
		{
			{
				"describe",
				"/app/stream/mobile",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(
						request_id,
						application,
						200,
						"ok",
						MakeDescribeResponsePayload(OVT_SIGNALING_VERSION));
				},
			},
			{
				"play",
				"/app/stream/mobile",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
			{
				"describe",
				"/app/stream",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(
						request_id,
						application,
						200,
						"ok",
						MakeDescribeResponsePayload(OVT_SIGNALING_VERSION));
				},
			},
		},
	});

	ASSERT_TRUE(server.Start()) << server.GetError().CStr();

	auto fixture = MakeNetworkProviderFixture(app_info, server.MakeUrl("/app/stream/mobile"));
	ASSERT_NE(fixture.stream, nullptr);

	fixture.stream->SetState(pvd::Stream::State::IDLE);
	fixture.stream->_compatibility_full_stream_mode_requested = true;
	fixture.stream->_compatibility_fallback_restart_pending	  = true;
	fixture.stream->RegisterDownstreamSession(
		1,
		ParseUrl(server.MakeUrl("/app/stream/mobile").CStr()),
		ParseUrl(server.MakeUrl("/app/stream/mobile").CStr()));

	EXPECT_TRUE(fixture.stream->RestartStream(
		ParseUrl(server.MakeUrl("/app/stream/mobile").CStr())));

	server.Stop();

	EXPECT_EQ(fixture.stream->GetState(), pvd::Stream::State::PLAYING);
	ASSERT_NE(fixture.stream->_curr_url, nullptr);
	EXPECT_EQ(fixture.stream->_curr_url->Path(), "/app/stream/mobile");
	EXPECT_EQ(fixture.stream->_curr_url->File(), "mobile");
	EXPECT_FALSE(fixture.stream->_compatibility_full_stream_mode_requested);
	EXPECT_FALSE(fixture.stream->_compatibility_reprobe_on_next_reconnect);
	EXPECT_FALSE(fixture.stream->_compatibility_fallback_restart_pending);
	EXPECT_EQ(fixture.stream->_inventory_snapshot_state, pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT);

	const auto requests = server.GetRequests();
	ASSERT_EQ(requests.size(), 3U);
	EXPECT_EQ(requests[0].target, server.MakeUrl("/app/stream/mobile"));
	EXPECT_EQ(requests[1].target, server.MakeUrl("/app/stream/mobile"));
	EXPECT_EQ(requests[2].target, server.MakeUrl("/app/stream"));
	EXPECT_TRUE(server.GetError().IsEmpty()) << server.GetError().CStr();
}

TEST(OvtProviderNetwork, DisconnectTriggersCollectorResumeWithStickyFullStreamTarget)
{
	auto app_info				= std::make_shared<TestApplicationInfo>("default", "app");
	const auto describe_payload = MakeDescribeResponsePayload(OVT_SIGNALING_VERSION);

	FakeOvtControlServer server({
		{
			{
				"describe",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream/mobile",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
		},
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
	});
	ASSERT_TRUE(server.Start()) << server.GetError().CStr();

	cfg::Server server_config;

	auto provider	 = std::make_shared<pvd::OvtProvider>(server_config, nullptr);
	auto application = std::make_shared<pvd::OvtApplication>(std::static_pointer_cast<pvd::PullProvider>(provider), *app_info);
	ASSERT_TRUE(application->Start());

	auto properties = std::make_shared<pvd::PullStreamProperties>();
	properties->SetRetryCount(2);

	const auto playlist_url = server.MakeUrl("/app/stream/mobile");
	auto stream				= std::make_shared<pvd::OvtStream>(
		application,
		MakeProviderStreamInfo(app_info),
		std::vector<ov::String>{playlist_url},
		properties);
	ASSERT_NE(stream, nullptr);

	stream->RegisterDownstreamSession(
		1,
		ParseUrl(playlist_url.CStr()),
		ParseUrl(playlist_url.CStr()));

	ASSERT_TRUE(stream->Start());
	stream->_compatibility_full_stream_mode_requested = true;

	ASSERT_TRUE(application->AddStream(stream));
	ASSERT_TRUE(application->AddStreamToMotorInternal(stream));

	ASSERT_TRUE(WaitUntil([&]() {
		return server.GetRequests().size() >= 5U;
	},
						  3000))
		<< server.GetError().CStr();

	const auto requests = server.GetRequests();
	ASSERT_EQ(requests.size(), 5U);
	EXPECT_EQ(requests[0].target, playlist_url);
	EXPECT_EQ(requests[1].target, playlist_url);
	EXPECT_EQ(requests[2].target, server.MakeUrl("/app/stream"));
	EXPECT_EQ(requests[3].target, server.MakeUrl("/app/stream"));
	EXPECT_EQ(requests[4].target, server.MakeUrl("/app/stream"));
	EXPECT_TRUE(server.GetError().IsEmpty()) << server.GetError().CStr();

	application->Stop();
	server.Stop();
}

TEST(OvtProviderNetwork, DisconnectTriggersFailoverRotationToNextOriginWithStickyFullStreamTarget)
{
	auto app_info				= std::make_shared<TestApplicationInfo>("default", "app");
	const auto describe_payload = MakeDescribeResponsePayload(OVT_SIGNALING_VERSION);

	FakeOvtControlServer primary_server({
		{
			{
				"describe",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream/mobile",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
		},
	});

	FakeOvtControlServer failover_server({
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
	});

	ASSERT_TRUE(primary_server.Start()) << primary_server.GetError().CStr();
	ASSERT_TRUE(failover_server.Start()) << failover_server.GetError().CStr();

	cfg::Server server_config;
	auto provider	 = std::make_shared<pvd::OvtProvider>(server_config, nullptr);
	auto application = std::make_shared<pvd::OvtApplication>(std::static_pointer_cast<pvd::PullProvider>(provider), *app_info);
	ASSERT_TRUE(application->Start());

	auto properties = std::make_shared<pvd::PullStreamProperties>();
	properties->SetRetryCount(2);

	const auto primary_playlist_url	 = primary_server.MakeUrl("/app/stream/mobile");
	const auto failover_playlist_url = failover_server.MakeUrl("/app/stream/mobile");

	auto stream						 = std::make_shared<pvd::OvtStream>(
		application,
		MakeProviderStreamInfo(app_info),
		std::vector<ov::String>{primary_playlist_url, failover_playlist_url},
		properties);
	ASSERT_NE(stream, nullptr);

	stream->RegisterDownstreamSession(
		1,
		ParseUrl(primary_playlist_url.CStr()),
		ParseUrl(primary_playlist_url.CStr()));

	ASSERT_TRUE(stream->Start());
	stream->_compatibility_full_stream_mode_requested = true;
	ASSERT_TRUE(application->AddStream(stream));
	ASSERT_TRUE(application->AddStreamToMotorInternal(stream));

	ASSERT_TRUE(WaitUntil([&]() {
		return primary_server.GetRequests().size() >= 3U && failover_server.GetRequests().size() >= 2U;
	},
						  3000))
		<< primary_server.GetError().CStr() << failover_server.GetError().CStr();

	const auto primary_requests = primary_server.GetRequests();
	ASSERT_EQ(primary_requests.size(), 3U);
	EXPECT_EQ(primary_requests[0].target, primary_playlist_url);
	EXPECT_EQ(primary_requests[1].target, primary_playlist_url);
	EXPECT_EQ(primary_requests[2].target, primary_server.MakeUrl("/app/stream"));

	const auto failover_requests = failover_server.GetRequests();
	ASSERT_EQ(failover_requests.size(), 2U);
	EXPECT_EQ(failover_requests[0].target, failover_server.MakeUrl("/app/stream"));
	EXPECT_EQ(failover_requests[1].target, failover_server.MakeUrl("/app/stream"));

	EXPECT_TRUE(primary_server.GetError().IsEmpty()) << primary_server.GetError().CStr();
	EXPECT_TRUE(failover_server.GetError().IsEmpty()) << failover_server.GetError().CStr();

	application->Stop();
	primary_server.Stop();
	failover_server.Stop();
}

TEST(OvtProviderNetwork, NoInputFailoverRotatesToNextOriginWithStickyFullStreamTarget)
{
	auto app_info				= std::make_shared<TestApplicationInfo>("default", "app");
	const auto describe_payload = MakeDescribeResponsePayload(OVT_SIGNALING_VERSION);

	FakeOvtControlServer primary_server({
		FakeOvtControlServer::ConnectionPlan{
			{
				{
					"describe",
					"/app/stream/mobile",
					[&](uint32_t request_id, const ov::String &application) {
						return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
					},
				},
				{
					"play",
					"/app/stream/mobile",
					[](uint32_t request_id, const ov::String &application) {
						return MakeControlResponsePayload(request_id, application, 200, "ok");
					},
				},
				{
					"describe",
					"/app/stream",
					[&](uint32_t request_id, const ov::String &application) {
						return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
					},
				},
			},
			true,
		},
	});

	FakeOvtControlServer failover_server({
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
	});

	ASSERT_TRUE(primary_server.Start()) << primary_server.GetError().CStr();
	ASSERT_TRUE(failover_server.Start()) << failover_server.GetError().CStr();

	cfg::Server server_config;
	MonitorInstance->Release();
	MonitorInstance->OnServerStarted(std::make_shared<cfg::Server>(server_config));
	ASSERT_TRUE(MonitorInstance->OnHostCreated(app_info->GetHostInfo()));
	ASSERT_TRUE(MonitorInstance->OnApplicationCreated(*app_info));

	auto provider	 = std::make_shared<pvd::OvtProvider>(server_config, nullptr);
	auto application = std::make_shared<pvd::OvtApplication>(std::static_pointer_cast<pvd::PullProvider>(provider), *app_info);
	ASSERT_TRUE(application->Start());

	auto properties = std::make_shared<pvd::PullStreamProperties>();
	properties->SetRetryCount(2);
	properties->SetNoInputFailoverTimeout(150);

	const auto primary_playlist_url	 = primary_server.MakeUrl("/app/stream/mobile");
	const auto failover_playlist_url = failover_server.MakeUrl("/app/stream/mobile");

	auto stream						 = std::make_shared<pvd::OvtStream>(
		application,
		MakeProviderStreamInfo(app_info),
		std::vector<ov::String>{primary_playlist_url, failover_playlist_url},
		properties);
	ASSERT_NE(stream, nullptr);

	stream->RegisterDownstreamSession(
		1,
		ParseUrl(primary_playlist_url.CStr()),
		ParseUrl(primary_playlist_url.CStr()));

	ASSERT_TRUE(stream->Start());
	stream->_compatibility_full_stream_mode_requested = true;

	ASSERT_TRUE(application->AddStream(stream));
	auto app_metrics = ApplicationMetrics(*app_info);
	ASSERT_NE(app_metrics, nullptr);
	ASSERT_TRUE(app_metrics->OnStreamCreated(*std::static_pointer_cast<info::Stream>(stream)));
	ASSERT_TRUE(application->AddStreamToMotorInternal(stream));

	auto stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(stream));
	ASSERT_NE(stream_metrics, nullptr);
	stream_metrics->_last_recv_time = std::chrono::system_clock::now() - std::chrono::milliseconds(1000);

	ASSERT_TRUE(WaitUntil([&]() {
		return primary_server.GetRequests().size() >= 3U && failover_server.GetRequests().size() >= 2U;
	},
						  3000))
		<< " primary_requests=" << DumpRequestTargets(primary_server.GetRequests()).CStr() << " failover_requests=" << DumpRequestTargets(failover_server.GetRequests()).CStr() << " primary_error=" << primary_server.GetError().CStr() << " failover_error=" << failover_server.GetError().CStr();

	const auto primary_requests = primary_server.GetRequests();
	ASSERT_EQ(primary_requests.size(), 3U);
	EXPECT_EQ(primary_requests[0].target, primary_playlist_url);
	EXPECT_EQ(primary_requests[1].target, primary_playlist_url);
	EXPECT_EQ(primary_requests[2].target, primary_server.MakeUrl("/app/stream"));

	const auto failover_requests = failover_server.GetRequests();
	ASSERT_EQ(failover_requests.size(), 2U);
	EXPECT_EQ(failover_requests[0].target, failover_server.MakeUrl("/app/stream"));
	EXPECT_EQ(failover_requests[1].target, failover_server.MakeUrl("/app/stream"));

	EXPECT_TRUE(primary_server.GetError().IsEmpty()) << primary_server.GetError().CStr();
	EXPECT_TRUE(failover_server.GetError().IsEmpty()) << failover_server.GetError().CStr();

	application->Stop();
	primary_server.Stop();
	failover_server.Stop();
	MonitorInstance->Release();
}

TEST(OvtProviderNetwork, DisconnectFailoverFallsBackToPrimaryWithStickyFullStreamTarget)
{
	auto app_info				= std::make_shared<TestApplicationInfo>("default", "app");
	const auto describe_payload = MakeDescribeResponsePayload(OVT_SIGNALING_VERSION);

	FakeOvtControlServer primary_server({
		{
			{
				"describe",
				"/app/stream/mobile",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream/mobile",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
		},
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
	});

	FakeOvtControlServer failover_server({
		{
			{
				"describe",
				"/app/stream",
				[&](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok", describe_payload);
				},
			},
			{
				"play",
				"/app/stream",
				[](uint32_t request_id, const ov::String &application) {
					return MakeControlResponsePayload(request_id, application, 200, "ok");
				},
			},
		},
	});

	ASSERT_TRUE(primary_server.Start()) << primary_server.GetError().CStr();
	ASSERT_TRUE(failover_server.Start()) << failover_server.GetError().CStr();

	cfg::Server server_config;
	auto provider	 = std::make_shared<pvd::OvtProvider>(server_config, nullptr);
	auto application = std::make_shared<pvd::OvtApplication>(std::static_pointer_cast<pvd::PullProvider>(provider), *app_info);
	ASSERT_TRUE(application->Start());

	auto properties = std::make_shared<pvd::PullStreamProperties>();
	properties->SetRetryCount(2);
	properties->EnableFailback(true);
	properties->SetStreamFailbackTimeout(150);

	const auto primary_playlist_url	 = primary_server.MakeUrl("/app/stream/mobile");
	const auto failover_playlist_url = failover_server.MakeUrl("/app/stream/mobile");

	auto stream						 = std::make_shared<pvd::OvtStream>(
		application,
		MakeProviderStreamInfo(app_info),
		std::vector<ov::String>{primary_playlist_url, failover_playlist_url},
		properties);
	ASSERT_NE(stream, nullptr);

	stream->RegisterDownstreamSession(
		1,
		ParseUrl(primary_playlist_url.CStr()),
		ParseUrl(primary_playlist_url.CStr()));

	ASSERT_TRUE(stream->Start());
	stream->_compatibility_full_stream_mode_requested = true;
	ASSERT_TRUE(application->AddStream(stream));
	ASSERT_TRUE(application->AddStreamToMotorInternal(stream));

	ASSERT_TRUE(WaitUntil([&]() {
		return primary_server.GetRequests().size() >= 7U && failover_server.GetRequests().size() >= 2U;
	},
						  4000))
		<< " primary_requests=" << DumpRequestTargets(primary_server.GetRequests()).CStr() << " failover_requests=" << DumpRequestTargets(failover_server.GetRequests()).CStr() << " primary_error=" << primary_server.GetError().CStr() << " failover_error=" << failover_server.GetError().CStr();

	const auto primary_requests = primary_server.GetRequests();
	ASSERT_EQ(primary_requests.size(), 7U);
	EXPECT_EQ(primary_requests[0].target, primary_playlist_url);
	EXPECT_EQ(primary_requests[1].target, primary_playlist_url);
	EXPECT_EQ(primary_requests[2].target, primary_server.MakeUrl("/app/stream"));
	EXPECT_EQ(primary_requests[3].target, primary_server.MakeUrl("/app/stream"));
	EXPECT_EQ(primary_requests[4].target, primary_server.MakeUrl("/app/stream"));
	EXPECT_EQ(primary_requests[5].target, primary_server.MakeUrl("/app/stream"));
	EXPECT_EQ(primary_requests[6].target, primary_server.MakeUrl("/app/stream"));

	const auto failover_requests = failover_server.GetRequests();
	ASSERT_EQ(failover_requests.size(), 2U);
	EXPECT_EQ(failover_requests[0].target, failover_server.MakeUrl("/app/stream"));
	EXPECT_EQ(failover_requests[1].target, failover_server.MakeUrl("/app/stream"));

	EXPECT_TRUE(stream->IsCurrPrimaryURL());
	EXPECT_TRUE(primary_server.GetError().IsEmpty()) << primary_server.GetError().CStr();
	EXPECT_TRUE(failover_server.GetError().IsEmpty()) << failover_server.GetError().CStr();

	application->Stop();
	primary_server.Stop();
	failover_server.Stop();
}
