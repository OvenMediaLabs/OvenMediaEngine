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
}  // namespace

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
}  // namespace

TEST(ScheduledLiveStartPolicy, LatestKeyframeReplaysFromTheKeyframe)
{
	auto policy = LiveStartPolicy::Create(Schedule::LiveStartPoint::LatestKeyframe);
	ASSERT_NE(policy, nullptr);
	EXPECT_TRUE(policy->NeedsPastData());
	EXPECT_TRUE(policy->PacesToChannelClock());

	auto keyframe = Video(0, true);
	auto plan	  = policy->Plan({Audio(-20), keyframe, Audio(10), Audio(31), Video(33), Video(66)}, MakeSource());

	EXPECT_TRUE(plan.starts_at_keyframe);
	ASSERT_EQ(plan.replay.size(), 5u);
	EXPECT_EQ(plan.replay.front(), keyframe);
	EXPECT_EQ(plan.replay_span_us, 66000);
}

TEST(ScheduledLiveStartPolicy, LatestKeyframeWithoutKeyframeReplaysNothing)
{
	auto policy = LiveStartPolicy::Create(Schedule::LiveStartPoint::LatestKeyframe);
	ASSERT_NE(policy, nullptr);

	for (const auto &past_data : {std::vector<std::shared_ptr<MediaPacket>>{Video(0), Audio(10), Video(33, true)},
								  std::vector<std::shared_ptr<MediaPacket>>{Audio(0), Audio(21)},
								  std::vector<std::shared_ptr<MediaPacket>>{}})
	{
		auto plan = policy->Plan(past_data, MakeSource());

		EXPECT_FALSE(plan.starts_at_keyframe);
		EXPECT_TRUE(plan.replay.empty());
	}
}

TEST(ScheduledLiveStartPolicy, NextKeyframeTakesNothingAndDoesNotPace)
{
	auto policy = LiveStartPolicy::Create(Schedule::LiveStartPoint::NextKeyframe);
	ASSERT_NE(policy, nullptr);

	EXPECT_FALSE(policy->NeedsPastData());
	EXPECT_FALSE(policy->PacesToChannelClock());

	auto plan = policy->Plan({Video(0, true), Audio(10)}, MakeSource());
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
