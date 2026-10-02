//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <config/config.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>

//  Covers the Whisper speech-to-text configuration after the move to CPU-only
//  inference:
//    - `<MaxThreads>` and `<Threads>` are the knobs that replace GPU selection.
//    - `<Devices>` and `<Modules>` no longer do anything, but configurations
//      written for the GPU build must keep loading instead of being rejected.
namespace
{
	// Writes <element>body</element> to a temporary file and parses it into item.
	// Returns false when the config layer rejects the document.
	template <typename Titem>
	bool ParseItem(const char *element, const std::string &body, Titem *item)
	{
		char path[]	 = "/tmp/ome_whisper_config_test_XXXXXX";
		const int fd = ::mkstemp(path);
		if (fd < 0)
		{
			return false;
		}
		::close(fd);

		{
			std::ofstream out(path);
			out << "<" << element << ">" << body << "</" << element << ">";
		}

		bool parsed = false;

		try
		{
			cfg::DataSource data_source(cfg::DataType::Xml, path, cfg::ItemName(element));
			item->FromDataSource(element, cfg::ItemName(element), data_source);
			parsed = true;
		}
		catch (const cfg::ConfigError &)
		{
			parsed = false;
		}

		::remove(path);

		return parsed;
	}

	bool ParseWhisper(const std::string &body, cfg::modules::Whisper *whisper)
	{
		return ParseItem("Whisper", body, whisper);
	}

	bool ParseSttRendition(const std::string &body, cfg::vhost::app::oprf::SttRendition *rendition)
	{
		return ParseItem("Rendition", body, rendition);
	}

	// The minimum an <STT><Rendition> needs; the three are mandatory.
	constexpr const char *kRenditionRequired =
		"<Engine>whisper</Engine>"
		"<OutputSubtitleLabel>Korean</OutputSubtitleLabel>"
		"<Model>ggml-base.en.bin</Model>";
}  // namespace

TEST(WhisperConfig, MaxThreadsDefaultsToAuto)
{
	cfg::modules::Whisper whisper;

	ASSERT_TRUE(ParseWhisper("<PreloadModel><Path>ggml-base.en.bin</Path></PreloadModel>", &whisper));
	// 0 means "derive from the hardware", which is what the registry expects.
	EXPECT_EQ(whisper.GetMaxThreads(), 0);
}

TEST(WhisperConfig, MaxThreadsKeepsTheConfiguredValue)
{
	cfg::modules::Whisper whisper;

	ASSERT_TRUE(ParseWhisper("<MaxThreads>6</MaxThreads>", &whisper));
	EXPECT_EQ(whisper.GetMaxThreads(), 6);
}

// <Devices> selected a GPU before Whisper moved to the CPU. It is ignored now,
// but a configuration written for the GPU build must still load.
TEST(WhisperConfig, PreloadModelStillAcceptsDevices)
{
	cfg::modules::Whisper whisper;

	ASSERT_TRUE(ParseWhisper(
		"<PreloadModel><Path>ggml-small.bin</Path><Devices>0,1</Devices></PreloadModel>"
		"<PreloadModel><Path>ggml-medium.bin</Path><Devices>all</Devices></PreloadModel>",
		&whisper));

	ASSERT_EQ(whisper.GetPreloadModels().size(), 2u);
	EXPECT_STREQ(whisper.GetPreloadModels()[0].GetPath().CStr(), "ggml-small.bin");
	EXPECT_STREQ(whisper.GetPreloadModels()[0].GetDevices().CStr(), "0,1");
	EXPECT_STREQ(whisper.GetPreloadModels()[1].GetDevices().CStr(), "all");
}

TEST(WhisperConfig, PreloadModelWorksWithoutDevices)
{
	cfg::modules::Whisper whisper;

	ASSERT_TRUE(ParseWhisper("<PreloadModel><Path>ggml-base.en.bin</Path></PreloadModel>", &whisper));

	ASSERT_EQ(whisper.GetPreloadModels().size(), 1u);
	EXPECT_TRUE(whisper.GetPreloadModels()[0].GetDevices().IsEmpty());
}

TEST(SttRenditionConfig, ThreadsDefaultsToAuto)
{
	cfg::vhost::app::oprf::SttRendition rendition;

	ASSERT_TRUE(ParseSttRendition(kRenditionRequired, &rendition));
	EXPECT_EQ(rendition.GetThreads(), 0);
}

TEST(SttRenditionConfig, ThreadsKeepsTheConfiguredValue)
{
	cfg::vhost::app::oprf::SttRendition rendition;

	ASSERT_TRUE(ParseSttRendition(std::string(kRenditionRequired) + "<Threads>4</Threads>", &rendition));
	EXPECT_EQ(rendition.GetThreads(), 4);
}

// <Modules>nv:N</Modules> picked a GPU before. The encoder ignores it now, but
// dropping it from the schema would break every configuration that has one.
TEST(SttRenditionConfig, StillAcceptsModules)
{
	cfg::vhost::app::oprf::SttRendition rendition;

	ASSERT_TRUE(ParseSttRendition(std::string(kRenditionRequired) + "<Modules>nv:1</Modules>", &rendition));
	EXPECT_STREQ(rendition.GetModules().CStr(), "nv:1");
}

// The sliding-window knobs share the item with the new <Threads>; make sure
// adding it did not disturb them.
TEST(SttRenditionConfig, WindowDefaultsAreUnchanged)
{
	cfg::vhost::app::oprf::SttRendition rendition;

	ASSERT_TRUE(ParseSttRendition(kRenditionRequired, &rendition));
	EXPECT_EQ(rendition.GetStepMs(), 2000);
	EXPECT_EQ(rendition.GetLengthMs(), 10000);
	EXPECT_EQ(rendition.GetKeepMs(), 1500);
}
