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
