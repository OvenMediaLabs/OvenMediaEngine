//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "live_start_policy.h"
#include "schedule.h"

namespace
{
	using pvd::Schedule;

	// stream:// items, so no media file is opened.
	Json::Value JsonItem(const Json::Value &fade)
	{
		Json::Value item = fade;
		item["url"]		 = "stream://app/live";
		return item;
	}

	std::tuple<std::shared_ptr<Schedule>, ov::String> LoadJson(const Json::Value &item)
	{
		Json::Value root;
		root["stream"]["name"] = "fade";
		root["fallbackProgram"]["items"].append(item);

		return Schedule::CreateFromJsonObject(root, "");
	}

	std::shared_ptr<Schedule::Item> FirstItem(const std::shared_ptr<Schedule> &schedule)
	{
		return schedule->GetFallbackProgram()->_items.front();
	}

	class ScheduledFadeXml : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			_dir = std::filesystem::temp_directory_path() / ("ome_scheduled_test_" + std::to_string(::getpid()));
			std::filesystem::create_directories(_dir);
		}

		void TearDown() override
		{
			std::filesystem::remove_all(_dir);
		}

		ov::String Path(const char *name) const
		{
			return (_dir / name).string().c_str();
		}

		std::tuple<std::shared_ptr<Schedule>, ov::String> LoadXml(const char *item_attributes)
		{
			auto path = Path("fade.xml");

			std::ofstream(path.CStr())
				<< "<Schedule><Stream><Name>fade</Name></Stream><FallbackProgram>"
				<< "<Item url=\"stream://app/live\" " << item_attributes << " />"
				<< "</FallbackProgram></Schedule>";

			return Schedule::CreateFromXMLFile(path, "");
		}

		std::filesystem::path _dir;
	};

	// Items of 10s each; the program started `elapsed_ms` ago.
	std::shared_ptr<Schedule::Program> MakeProgram(int item_count, int64_t elapsed_ms, bool repeat)
	{
		auto program			 = std::make_shared<Schedule::Program>();
		program->_scheduled_time = std::chrono::system_clock::now() - std::chrono::milliseconds(elapsed_ms);
		program->_repeat		 = repeat;

		for (int i = 0; i < item_count; i++)
		{
			auto item				  = std::make_shared<Schedule::Item>();
			item->_url				  = ov::String::FormatString("file://item%d.mp4", i);
			item->_start_time_ms_conf = 0;
			item->_duration_ms		  = 10000;
			program->_items.push_back(item);
		}

		program->_total_item_duration_ms = item_count * 10000;

		return program;
	}
}  // namespace

TEST(ScheduledProgramPosition, ReentryPicksTheItemAtTheCurrentTime)
{
	auto program = MakeProgram(3, 15000, /*repeat=*/true);

	auto first = program->GetFirstItemWithPosition();
	ASSERT_NE(first, nullptr);
	EXPECT_STREQ(first->_url.CStr(), "file://item1.mp4");
	EXPECT_STREQ(program->GetNextItem()->_url.CStr(), "file://item2.mp4");

	// Recover on the same program, now 25s in
	program->_scheduled_time -= std::chrono::milliseconds(10000);

	auto again = program->GetFirstItemWithPosition();
	ASSERT_NE(again, nullptr);
	EXPECT_STREQ(again->_url.CStr(), "file://item2.mp4");
	EXPECT_NEAR(again->_start_time_ms, 5000, 100);
	EXPECT_STREQ(program->GetNextItem()->_url.CStr(), "file://item0.mp4");
}

TEST(ScheduledProgramPosition, RepeatProgramWrapsPastTheLastItem)
{
	auto program = MakeProgram(2, 25000, /*repeat=*/true);

	auto first = program->GetFirstItemWithPosition();
	ASSERT_NE(first, nullptr);
	EXPECT_STREQ(first->_url.CStr(), "file://item0.mp4");
	EXPECT_NEAR(first->_start_time_ms, 5000, 100);
}

TEST(ScheduledProgramPosition, NonRepeatProgramIsOffAirPastTheLastItem)
{
	auto program = MakeProgram(2, 25000, /*repeat=*/false);

	EXPECT_EQ(program->GetFirstItemWithPosition(), nullptr);
	EXPECT_TRUE(program->IsOffAir());
}

TEST(ScheduledStallTolerance, DefaultsToTenSeconds)
{
	auto [schedule, error] = LoadJson(JsonItem(Json::objectValue));
	ASSERT_NE(schedule, nullptr) << error.CStr();
	EXPECT_EQ(schedule->GetStream()._stall_tolerance_duration_ms, 10000);
}

TEST(ScheduledStallTolerance, JsonRoundTrip)
{
	Json::Value root;
	root["stream"]["name"]					   = "stall_tolerance";
	root["stream"]["stallToleranceDurationMs"] = 0;
	root["fallbackProgram"]["items"].append(JsonItem(Json::objectValue));

	auto [schedule, error] = Schedule::CreateFromJsonObject(root, "");
	ASSERT_NE(schedule, nullptr) << error.CStr();
	EXPECT_EQ(schedule->GetStream()._stall_tolerance_duration_ms, 0);

	Json::Value saved;
	ASSERT_EQ(schedule->ToJsonObject(saved), CommonErrorCode::SUCCESS);
	EXPECT_EQ(saved["stream"]["stallToleranceDurationMs"].asInt64(), 0);
}

TEST(ScheduledStallTolerance, JsonRejectsNonInteger)
{
	Json::Value root;
	root["stream"]["name"]					   = "stall_tolerance";
	root["stream"]["stallToleranceDurationMs"] = "10s";
	root["fallbackProgram"]["items"].append(JsonItem(Json::objectValue));

	auto [schedule, error] = Schedule::CreateFromJsonObject(root, "");
	EXPECT_EQ(schedule, nullptr);
}

TEST(ScheduledFadeJson, RoundTrip)
{
	Json::Value fade;
	fade["fadeIn"]		 = 1000;
	fade["fadeInColor"]	 = "white";
	fade["fadeOut"]		 = 2000;
	fade["fadeOutColor"] = "black";

	auto [schedule, error] = LoadJson(JsonItem(fade));
	ASSERT_NE(schedule, nullptr) << error.CStr();

	Json::Value saved;
	ASSERT_EQ(schedule->ToJsonObject(saved), CommonErrorCode::SUCCESS);

	const auto &item = saved["fallbackProgram"]["items"][0];
	EXPECT_EQ(item["fadeIn"].asInt64(), 1000);
	EXPECT_EQ(item["fadeInColor"].asString(), "white");
	EXPECT_EQ(item["fadeOut"].asInt64(), 2000);
	EXPECT_EQ(item["fadeOutColor"].asString(), "black");

	auto [reloaded, reload_error] = Schedule::CreateFromJsonObject(saved, "");
	ASSERT_NE(reloaded, nullptr) << reload_error.CStr();
	EXPECT_TRUE(*FirstItem(reloaded) == *FirstItem(schedule));
}

TEST(ScheduledFadeJson, RejectsInvalidColor)
{
	for (const char *color : {"red", "#ffffff"})
	{
		Json::Value fade;
		fade["fadeIn"]		= 1000;
		fade["fadeInColor"] = color;

		auto [schedule, error] = LoadJson(JsonItem(fade));
		EXPECT_EQ(schedule, nullptr) << color;
		EXPECT_NE(error.IndexOf("fadeInColor"), -1) << error.CStr();
	}
}

TEST(ScheduledFadeJson, ClampsNegativeLength)
{
	Json::Value fade;
	fade["fadeIn"]	= -500;
	fade["fadeOut"] = -1;

	auto [schedule, error] = LoadJson(JsonItem(fade));
	ASSERT_NE(schedule, nullptr) << error.CStr();

	EXPECT_EQ(FirstItem(schedule)->_fade._in_ms, 0);
	EXPECT_EQ(FirstItem(schedule)->_fade._out_ms, 0);
}

TEST(ScheduledFadeJson, SavesColorOnly)
{
	Json::Value fade;
	fade["fadeInColor"] = "white";

	auto [schedule, error] = LoadJson(JsonItem(fade));
	ASSERT_NE(schedule, nullptr) << error.CStr();

	Json::Value saved;
	ASSERT_EQ(schedule->ToJsonObject(saved), CommonErrorCode::SUCCESS);

	const auto &item = saved["fallbackProgram"]["items"][0];
	EXPECT_EQ(item["fadeInColor"].asString(), "white");
	EXPECT_FALSE(item.isMember("fadeIn"));
}

TEST(ScheduledFadeJson, ComparesColorsByValue)
{
	auto load = [](const char *color) {
		Json::Value fade;
		fade["fadeIn"] = 1000;
		if (color != nullptr)
		{
			fade["fadeInColor"] = color;
		}

		auto [schedule, error] = LoadJson(JsonItem(fade));
		EXPECT_NE(schedule, nullptr) << error.CStr();
		return FirstItem(schedule);
	};

	auto unset = load(nullptr);
	EXPECT_TRUE(*unset == *load("black"));
	EXPECT_TRUE(*unset == *load("BLACK"));
	EXPECT_FALSE(*unset == *load("white"));
}

TEST_F(ScheduledFadeXml, RoundTrip)
{
	auto [schedule, error] = LoadXml(R"(fadeIn="1000" fadeInColor="white" fadeOut="2000" fadeOutColor="black")");
	ASSERT_NE(schedule, nullptr) << error.CStr();

	auto saved_path = Path("saved.xml");
	ASSERT_EQ(schedule->SaveToXMLFile(saved_path), CommonErrorCode::SUCCESS);

	auto [reloaded, reload_error] = Schedule::CreateFromXMLFile(saved_path, "");
	ASSERT_NE(reloaded, nullptr) << reload_error.CStr();

	const auto &fade = FirstItem(reloaded)->_fade;
	EXPECT_EQ(fade._in_ms, 1000);
	EXPECT_EQ(fade._in_color._text, "white");
	EXPECT_EQ(fade._out_ms, 2000);
	EXPECT_EQ(fade._out_color._text, "black");
	EXPECT_TRUE(*FirstItem(reloaded) == *FirstItem(schedule));
}

TEST_F(ScheduledFadeXml, RejectsInvalidColor)
{
	for (const char *attributes : {R"(fadeIn="1000" fadeInColor="red")", R"(fadeIn="1000" fadeInColor="#ffffff")"})
	{
		auto [schedule, error] = LoadXml(attributes);
		EXPECT_EQ(schedule, nullptr) << attributes;
		EXPECT_NE(error.IndexOf("fadeInColor"), -1) << error.CStr();
	}
}

TEST_F(ScheduledFadeXml, ClampsNegativeLength)
{
	auto [schedule, error] = LoadXml(R"(fadeIn="-500" fadeOut="-1")");
	ASSERT_NE(schedule, nullptr) << error.CStr();

	EXPECT_EQ(FirstItem(schedule)->_fade._in_ms, 0);
	EXPECT_EQ(FirstItem(schedule)->_fade._out_ms, 0);
}

TEST_F(ScheduledFadeXml, SavesColorOnly)
{
	auto [schedule, error] = LoadXml(R"(fadeInColor="white")");
	ASSERT_NE(schedule, nullptr) << error.CStr();

	auto saved_path = Path("saved.xml");
	ASSERT_EQ(schedule->SaveToXMLFile(saved_path), CommonErrorCode::SUCCESS);

	pugi::xml_document saved;
	ASSERT_TRUE(saved.load_file(saved_path.CStr()));

	auto item = saved.child("Schedule").child("FallbackProgram").child("Item");
	EXPECT_STREQ(item.attribute("fadeInColor").as_string(), "white");
	EXPECT_FALSE(item.attribute("fadeIn"));
}

namespace
{
	// stream:// items, so no media file is opened.
	std::tuple<std::shared_ptr<Schedule>, ov::String> LoadJsonLiveStartPoint(const Json::Value &live_start_point)
	{
		Json::Value root;
		root["stream"]["name"] = "live";
		if (live_start_point.isNull() == false)
		{
			root["stream"]["liveStartPoint"] = live_start_point;
		}

		Json::Value item;
		item["url"] = "stream://app/live";
		root["fallbackProgram"]["items"].append(item);

		return Schedule::CreateFromJsonObject(root, "");
	}

	class ScheduledLiveStartPointXml : public ScheduledFadeXml
	{
	protected:
		std::tuple<std::shared_ptr<Schedule>, ov::String> LoadStreamXml(const char *stream_children)
		{
			auto path = Path("live.xml");

			std::ofstream(path.CStr())
				<< "<Schedule><Stream><Name>live</Name>" << stream_children << "</Stream><FallbackProgram>"
				<< "<Item url=\"stream://app/live\" />"
				<< "</FallbackProgram></Schedule>";

			return Schedule::CreateFromXMLFile(path, "");
		}
	};
}  // namespace

TEST(ScheduledLiveStartPointJson, DefaultsToLatestKeyframe)
{
	auto [schedule, error] = LoadJsonLiveStartPoint(Json::Value());
	ASSERT_NE(schedule, nullptr) << error.CStr();

	EXPECT_EQ(schedule->GetStream()._live_start_point, Schedule::LiveStartPoint::LatestKeyframe);
}

TEST(ScheduledLiveStartPointJson, AcceptsValuesIgnoringCase)
{
	auto [latest, latest_error] = LoadJsonLiveStartPoint("latestkeyframe");
	ASSERT_NE(latest, nullptr) << latest_error.CStr();
	EXPECT_EQ(latest->GetStream()._live_start_point, Schedule::LiveStartPoint::LatestKeyframe);

	auto [next, next_error] = LoadJsonLiveStartPoint("NextKeyframe");
	ASSERT_NE(next, nullptr) << next_error.CStr();
	EXPECT_EQ(next->GetStream()._live_start_point, Schedule::LiveStartPoint::NextKeyframe);
}

TEST(ScheduledLiveStartPointJson, RejectsUnknownValue)
{
	for (const Json::Value &value : {Json::Value("CurrentFrame"), Json::Value(1)})
	{
		auto [schedule, error] = LoadJsonLiveStartPoint(value);
		EXPECT_EQ(schedule, nullptr) << value.toStyledString();
		EXPECT_NE(error.IndexOf("liveStartPoint"), -1) << error.CStr();
	}
}

TEST(ScheduledLiveStartPointJson, RoundTrip)
{
	auto [schedule, error] = LoadJsonLiveStartPoint("NextKeyframe");
	ASSERT_NE(schedule, nullptr) << error.CStr();

	Json::Value saved;
	ASSERT_EQ(schedule->ToJsonObject(saved), CommonErrorCode::SUCCESS);
	EXPECT_EQ(saved["stream"]["liveStartPoint"].asString(), "NextKeyframe");

	auto [reloaded, reload_error] = Schedule::CreateFromJsonObject(saved, "");
	ASSERT_NE(reloaded, nullptr) << reload_error.CStr();
	EXPECT_EQ(reloaded->GetStream()._live_start_point, Schedule::LiveStartPoint::NextKeyframe);
}

// A running channel rejects a <Stream> change, so the value must be part of the comparison
TEST(ScheduledLiveStartPointJson, IsComparedAsPartOfTheStream)
{
	auto [latest, latest_error] = LoadJsonLiveStartPoint("LatestKeyframe");
	ASSERT_NE(latest, nullptr) << latest_error.CStr();

	auto [next, next_error] = LoadJsonLiveStartPoint("NextKeyframe");
	ASSERT_NE(next, nullptr) << next_error.CStr();

	auto [same, same_error] = LoadJsonLiveStartPoint("LatestKeyframe");
	ASSERT_NE(same, nullptr) << same_error.CStr();

	EXPECT_TRUE(latest->GetStream() != next->GetStream());
	EXPECT_TRUE(latest->GetStream() == same->GetStream());
}

TEST_F(ScheduledLiveStartPointXml, DefaultsToLatestKeyframe)
{
	auto [schedule, error] = LoadStreamXml("");
	ASSERT_NE(schedule, nullptr) << error.CStr();

	EXPECT_EQ(schedule->GetStream()._live_start_point, Schedule::LiveStartPoint::LatestKeyframe);
}

TEST_F(ScheduledLiveStartPointXml, RejectsUnknownValue)
{
	auto [schedule, error] = LoadStreamXml("<LiveStartPoint>CurrentFrame</LiveStartPoint>");
	EXPECT_EQ(schedule, nullptr);
	EXPECT_NE(error.IndexOf("LiveStartPoint"), -1) << error.CStr();
}

TEST_F(ScheduledLiveStartPointXml, RoundTrip)
{
	auto [schedule, error] = LoadStreamXml("<LiveStartPoint> nextkeyframe </LiveStartPoint>");
	ASSERT_NE(schedule, nullptr) << error.CStr();
	EXPECT_EQ(schedule->GetStream()._live_start_point, Schedule::LiveStartPoint::NextKeyframe);

	auto saved_path = Path("saved.xml");
	ASSERT_EQ(schedule->SaveToXMLFile(saved_path), CommonErrorCode::SUCCESS);

	pugi::xml_document saved;
	ASSERT_TRUE(saved.load_file(saved_path.CStr()));
	EXPECT_STREQ(saved.child("Schedule").child("Stream").child("LiveStartPoint").text().as_string(), "NextKeyframe");

	auto [reloaded, reload_error] = Schedule::CreateFromXMLFile(saved_path, "");
	ASSERT_NE(reloaded, nullptr) << reload_error.CStr();
	EXPECT_EQ(reloaded->GetStream()._live_start_point, Schedule::LiveStartPoint::NextKeyframe);
}

namespace
{
	using pvd::LiveStartPolicy;

	constexpr uint32_t kVideoTrackId = 0;
	constexpr uint32_t kAudioTrackId = 1;
	constexpr uint32_t kDataTrackId	 = 2;

	std::shared_ptr<info::Stream> MakeSource()
	{
		auto source = std::make_shared<info::Stream>(StreamSourceType::Rtmp);

		auto video = std::make_shared<MediaTrack>();
		video->SetId(kVideoTrackId);
		video->SetMediaType(cmn::MediaType::Video);
		video->SetTimeBase(1, 90000);
		source->AddTrack(video);

		auto audio = std::make_shared<MediaTrack>();
		audio->SetId(kAudioTrackId);
		audio->SetMediaType(cmn::MediaType::Audio);
		audio->SetTimeBase(1, 48000);
		source->AddTrack(audio);

		auto data = std::make_shared<MediaTrack>();
		data->SetId(kDataTrackId);
		data->SetMediaType(cmn::MediaType::Data);
		data->SetTimeBase(1, 1000);
		source->AddTrack(data);

		return source;
	}

	std::shared_ptr<MediaPacket> MakePacket(cmn::MediaType media_type, uint32_t track_id, int64_t dts, bool keyframe)
	{
		return std::make_shared<MediaPacket>(media_type, track_id, nullptr, dts, dts, 0,
											 keyframe ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
											 cmn::BitstreamFormat::Unknown, cmn::PacketType::Unknown);
	}

	std::shared_ptr<MediaPacket> Video(int64_t dts_ms, bool keyframe = false)
	{
		return MakePacket(cmn::MediaType::Video, kVideoTrackId, dts_ms * 90, keyframe);
	}

	std::shared_ptr<MediaPacket> Audio(int64_t dts_ms)
	{
		return MakePacket(cmn::MediaType::Audio, kAudioTrackId, dts_ms * 48, false);
	}

	std::shared_ptr<MediaPacket> Data(int64_t dts_ms)
	{
		return MakePacket(cmn::MediaType::Data, kDataTrackId, dts_ms, false);
	}

	using TappedPacket = MediaRouterStreamTap::TappedPacket;

	// The packets as the tap hands them over, received in the given order 1 ms apart
	std::vector<TappedPacket> Received(const std::vector<std::shared_ptr<MediaPacket>> &packets)
	{
		std::vector<TappedPacket> received;
		auto received_time = std::chrono::steady_clock::time_point{};

		for (const auto &packet : packets)
		{
			received.push_back(TappedPacket{packet, received_time});
			received_time += std::chrono::milliseconds(1);
		}

		return received;
	}
}  // namespace

TEST(ScheduledLiveStartPolicy, LatestKeyframeReplaysFromTheKeyframe)
{
	auto policy = LiveStartPolicy::Create(Schedule::LiveStartPoint::LatestKeyframe);
	ASSERT_NE(policy, nullptr);
	EXPECT_TRUE(policy->NeedsPastData());

	auto keyframe  = Video(0, true);
	auto past_data = Received({Audio(-20), keyframe, Audio(10), Audio(31), Video(33), Video(66)});
	auto plan	   = policy->Plan(past_data, MakeSource());

	EXPECT_TRUE(plan.starts_at_keyframe);
	ASSERT_EQ(plan.replay.size(), 5u);
	EXPECT_EQ(plan.replay.front().packet, keyframe);

	// Each packet goes out at its timestamp's distance from the keyframe
	std::vector<int64_t> release_offsets;
	for (const auto &replayed : plan.replay)
	{
		release_offsets.push_back(replayed.release_offset_us);
	}
	EXPECT_EQ(release_offsets, (std::vector<int64_t>{0, 10000, 31000, 33000, 66000}));
	EXPECT_EQ(plan.replay_span_us, 66000);
}

// A source that stalled and then sent its backlog at once still replays evenly
TEST(ScheduledLiveStartPolicy, LatestKeyframeReplaysByTimestampsHoweverItArrived)
{
	auto policy = LiveStartPolicy::Create(Schedule::LiveStartPoint::LatestKeyframe);
	ASSERT_NE(policy, nullptr);

	auto burst_time = std::chrono::steady_clock::time_point{};
	auto plan		= policy->Plan({TappedPacket{Video(0, true), burst_time}, TappedPacket{Video(33), burst_time}, TappedPacket{Video(66), burst_time}}, MakeSource());

	ASSERT_EQ(plan.replay.size(), 3u);
	EXPECT_EQ(plan.replay[0].release_offset_us, 0);
	EXPECT_EQ(plan.replay[1].release_offset_us, 33000);
	EXPECT_EQ(plan.replay[2].release_offset_us, 66000);
}

// Data can be stamped ahead, such as an event with a start offset, so it goes out with the packet before it
TEST(ScheduledLiveStartPolicy, LatestKeyframeDoesNotHoldDataForItsStamp)
{
	auto policy = LiveStartPolicy::Create(Schedule::LiveStartPoint::LatestKeyframe);
	ASSERT_NE(policy, nullptr);

	auto plan = policy->Plan(Received({Video(0, true), Audio(10), Video(33), Data(15000)}), MakeSource());

	ASSERT_EQ(plan.replay.size(), 4u);
	EXPECT_EQ(plan.replay[3].release_offset_us, 33000);
	EXPECT_EQ(plan.replay_span_us, 33000);
}

TEST(ScheduledLiveStartPolicy, LatestKeyframeWithoutKeyframeReplaysNothing)
{
	auto policy = LiveStartPolicy::Create(Schedule::LiveStartPoint::LatestKeyframe);
	ASSERT_NE(policy, nullptr);

	for (const auto &past_data : {Received({Video(0), Audio(10), Video(33, true)}),
								  Received({Audio(0), Audio(21)}),
								  Received({})})
	{
		auto plan = policy->Plan(past_data, MakeSource());

		EXPECT_FALSE(plan.starts_at_keyframe);
		EXPECT_TRUE(plan.replay.empty());
	}
}

TEST(ScheduledLiveStartPolicy, NextKeyframeTakesNothing)
{
	auto policy = LiveStartPolicy::Create(Schedule::LiveStartPoint::NextKeyframe);
	ASSERT_NE(policy, nullptr);

	EXPECT_FALSE(policy->NeedsPastData());

	auto plan = policy->Plan(Received({Video(0, true), Audio(10)}), MakeSource());
	EXPECT_FALSE(plan.starts_at_keyframe);
	EXPECT_TRUE(plan.replay.empty());
}

namespace
{
	class ScheduledStreamXml : public ScheduledLiveStartPointXml
	{
	};
}  // namespace

TEST(ScheduledStreamJson, RejectsNeitherVideoNorAudio)
{
	Json::Value root;
	root["stream"]["name"]		 = "none";
	root["stream"]["videoTrack"] = false;
	root["stream"]["audioTrack"] = false;

	Json::Value item;
	item["url"] = "stream://app/live";
	root["fallbackProgram"]["items"].append(item);

	auto [schedule, error] = Schedule::CreateFromJsonObject(root, "");
	EXPECT_EQ(schedule, nullptr);
	EXPECT_NE(error.IndexOf("cannot both be false"), -1) << error.CStr();
}

TEST_F(ScheduledStreamXml, RejectsNeitherVideoNorAudio)
{
	auto [schedule, error] = LoadStreamXml("<VideoTrack>false</VideoTrack><AudioTrack>false</AudioTrack>");
	EXPECT_EQ(schedule, nullptr);
	EXPECT_NE(error.IndexOf("cannot both be false"), -1) << error.CStr();
}

TEST_F(ScheduledStreamXml, StallToleranceRoundTrip)
{
	auto path = Path("stall_tolerance.xml");
	std::ofstream(path.CStr())
		<< "<Schedule><Stream><Name>stall_tolerance</Name><StallToleranceDurationMs>20000</StallToleranceDurationMs></Stream>"
		<< "<FallbackProgram><Item url=\"stream://app/live\" /></FallbackProgram></Schedule>";

	auto [schedule, error] = Schedule::CreateFromXMLFile(path, "");
	ASSERT_NE(schedule, nullptr) << error.CStr();
	EXPECT_EQ(schedule->GetStream()._stall_tolerance_duration_ms, 20000);

	auto saved_path = Path("saved.xml");
	ASSERT_EQ(schedule->SaveToXMLFile(saved_path), CommonErrorCode::SUCCESS);

	auto [reloaded, reload_error] = Schedule::CreateFromXMLFile(saved_path, "");
	ASSERT_NE(reloaded, nullptr) << reload_error.CStr();
	EXPECT_EQ(reloaded->GetStream()._stall_tolerance_duration_ms, 20000);
}
