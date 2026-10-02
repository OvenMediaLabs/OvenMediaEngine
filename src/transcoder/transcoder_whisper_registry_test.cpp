//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: WhisperModelRegistry against real model files.
//
//  These tests need whisper.cpp model files and a 16 kHz mono PCM16 WAV, so
//  they are skipped unless the environment provides them:
//
//    OME_WHISPER_TEST_MODELS=/path/ggml-tiny.en.bin:/path/ggml-base.en.bin
//    OME_WHISPER_TEST_WAV=/path/jfk.wav
//
//  The first model is used for inference; a second one, when given, lets the
//  concurrent-load test exercise two different models at once.
//
//==============================================================================
#include <gtest/gtest.h>
#include <whisper.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "transcoder_whisper_model_registry.h"

namespace
{
	std::vector<std::string> SplitPaths(const char *value)
	{
		std::vector<std::string> paths;
		if (value == nullptr)
		{
			return paths;
		}
		std::string current;
		for (const char *p = value; ; ++p)
		{
			if (*p == ':' || *p == '\0')
			{
				if (current.empty() == false)
				{
					paths.push_back(current);
				}
				current.clear();
				if (*p == '\0')
				{
					break;
				}
			}
			else
			{
				current.push_back(*p);
			}
		}
		return paths;
	}

	// Minimal RIFF/WAVE reader for 16 kHz mono PCM16 (what whisper.cpp's
	// samples/jfk.wav is). Returns false on anything else.
	bool ReadPcm16MonoWav(const std::string &path, std::vector<float> *samples)
	{
		std::ifstream in(path, std::ios::binary);
		if (in.is_open() == false)
		{
			return false;
		}

		char riff[12];
		if (in.read(riff, sizeof(riff)).gcount() != static_cast<std::streamsize>(sizeof(riff)) ||
			std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
		{
			return false;
		}

		uint16_t channels = 0, bits = 0;
		uint32_t rate	  = 0;
		while (in.good())
		{
			char id[4];
			uint32_t size = 0;
			if (in.read(id, 4).gcount() != 4 || in.read(reinterpret_cast<char *>(&size), 4).gcount() != 4)
			{
				return false;
			}

			if (std::memcmp(id, "fmt ", 4) == 0)
			{
				std::vector<char> fmt(size);
				in.read(fmt.data(), size);
				std::memcpy(&channels, fmt.data() + 2, 2);
				std::memcpy(&rate, fmt.data() + 4, 4);
				std::memcpy(&bits, fmt.data() + 14, 2);
			}
			else if (std::memcmp(id, "data", 4) == 0)
			{
				if (channels != 1 || rate != WHISPER_SAMPLE_RATE || bits != 16)
				{
					return false;
				}
				std::vector<int16_t> pcm(size / 2);
				in.read(reinterpret_cast<char *>(pcm.data()), size);
				samples->resize(pcm.size());
				for (size_t i = 0; i < pcm.size(); ++i)
				{
					(*samples)[i] = static_cast<float>(pcm[i]) / 32768.0f;
				}
				return samples->empty() == false;
			}
			else
			{
				in.seekg(size + (size & 1), std::ios::cur);
			}
		}
		return false;
	}

	// Greedy, deterministic parameters: every state fed the same audio must
	// produce the same text, which is how the concurrency test tells a race
	// from a correct run.
	whisper_full_params DeterministicParams(int32_t n_threads)
	{
		whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
		wparams.print_progress		= false;
		wparams.print_special		= false;
		wparams.print_realtime		= false;
		wparams.print_timestamps	= false;
		wparams.language			= "en";
		wparams.n_threads			= n_threads;
		wparams.no_context			= true;
		wparams.single_segment		= false;
		wparams.temperature_inc		= 0.0f;
		wparams.greedy.best_of		= 1;
		wparams.beam_search.beam_size = -1;
		return wparams;
	}

	std::string CollectText(whisper_state *state)
	{
		std::string text;
		const int n_segments = whisper_full_n_segments_from_state(state);
		for (int i = 0; i < n_segments; ++i)
		{
			text += whisper_full_get_segment_text_from_state(state, i);
		}
		return text;
	}

	std::string Lower(std::string s)
	{
		for (auto &c : s)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return s;
	}
}  // namespace

class WhisperRegistryModelTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		_models = SplitPaths(std::getenv("OME_WHISPER_TEST_MODELS"));
		const char *wav = std::getenv("OME_WHISPER_TEST_WAV");
		if (_models.empty() || wav == nullptr)
		{
			GTEST_SKIP() << "set OME_WHISPER_TEST_MODELS and OME_WHISPER_TEST_WAV to run";
		}
		ASSERT_TRUE(ReadPcm16MonoWav(wav, &_samples)) << "expected a 16 kHz mono PCM16 WAV: " << wav;
		_registry = WhisperModelRegistry::GetInstance();
	}

	void TearDown() override
	{
		if (_registry != nullptr)
		{
			_registry->SetMaxThreads(0);
		}
	}

	ov::String Primary() const
	{
		return ov::String(_models[0].c_str());
	}

	std::vector<std::string> _models;
	std::vector<float> _samples;
	WhisperModelRegistry *_registry = nullptr;
};

TEST_F(WhisperRegistryModelTest, LoadsOnDemandAndSharesOneContext)
{
	auto first = _registry->GetModelContext(Primary());
	ASSERT_NE(first, nullptr);

	auto second = _registry->GetModelContext(Primary());
	EXPECT_EQ(first.get(), second.get()) << "the same model must be loaded once and shared";

	// The device id is accepted for compatibility and must not fork the cache.
	auto on_device_3 = _registry->GetModelContext(Primary(), 3);
	EXPECT_EQ(first.get(), on_device_3.get());
}

TEST_F(WhisperRegistryModelTest, ConcurrentCallersLoadEachModelOnce)
{
	if (_models.size() < 2)
	{
		GTEST_SKIP() << "give two models to exercise concurrent loads";
	}

	// Three threads: A, B, A. Loads run outside the registry lock but one at a
	// time; the second A must wait for the first instead of loading again.
	std::shared_ptr<whisper_context> a1, a2, b;
	std::thread ta1([&] { a1 = _registry->GetModelContext(ov::String(_models[0].c_str())); });
	std::thread tb([&] { b = _registry->GetModelContext(ov::String(_models[1].c_str())); });
	std::thread ta2([&] { a2 = _registry->GetModelContext(ov::String(_models[0].c_str())); });
	ta1.join();
	tb.join();
	ta2.join();

	ASSERT_NE(a1, nullptr);
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(a1.get(), a2.get()) << "two callers of the same model must share one context";
	EXPECT_NE(a1.get(), b.get());
}

TEST_F(WhisperRegistryModelTest, OneInferenceTranscribesTheSample)
{
	ASSERT_NE(_registry->GetModelContext(Primary()), nullptr);
	auto *state = _registry->NewState(Primary());
	ASSERT_NE(state, nullptr);

	auto ctx = _registry->GetModelContext(Primary());
	ASSERT_EQ(whisper_full_with_state(ctx.get(), state, DeterministicParams(_registry->GetThreadShare(0)), _samples.data(), static_cast<int>(_samples.size())), 0);
	_registry->MarkStateResident(state);

	const std::string text = Lower(CollectText(state));
	EXPECT_FALSE(text.empty());
	// jfk.wav: "And so my fellow Americans, ask not what your country can do for you ..."
	EXPECT_NE(text.find("ask not"), std::string::npos) << "got: " << text;

	_registry->DeleteState(state);
}

// Several encoders share one whisper_context, each with its own whisper_state,
// and run whisper_full_with_state() at the same time - exactly what concurrent
// STT tracks on the same model do. A race would show up as a crash, a non-zero
// return, or diverging text for identical input.
TEST_F(WhisperRegistryModelTest, ConcurrentInferenceOnOneContextIsConsistent)
{
	constexpr int kStreams = 4;
	constexpr int kRounds  = 3;

	auto ctx = _registry->GetModelContext(Primary());
	ASSERT_NE(ctx, nullptr);

	std::vector<whisper_state *> states;
	for (int i = 0; i < kStreams; ++i)
	{
		auto *state = _registry->NewState(Primary());
		ASSERT_NE(state, nullptr) << "state " << i;
		states.push_back(state);
	}

	const int32_t threads = _registry->GetThreadShare(0);
	std::vector<std::string> texts(kStreams);
	std::vector<int> results(kStreams, -1);
	std::atomic<int> failures{0};

	const auto begin = std::chrono::steady_clock::now();
	std::vector<std::thread> workers;
	for (int i = 0; i < kStreams; ++i)
	{
		workers.emplace_back([&, i] {
			for (int round = 0; round < kRounds; ++round)
			{
				results[i] = whisper_full_with_state(ctx.get(), states[i], DeterministicParams(threads), _samples.data(), static_cast<int>(_samples.size()));
				if (results[i] != 0)
				{
					failures.fetch_add(1);
					return;
				}
				_registry->MarkStateResident(states[i]);

				const std::string text = CollectText(states[i]);
				if (round == 0)
				{
					texts[i] = text;
				}
				else if (text != texts[i])
				{
					failures.fetch_add(1);  // the same state, same audio, different text
				}
			}
		});
	}
	for (auto &w : workers)
	{
		w.join();
	}
	const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();

	EXPECT_EQ(failures.load(), 0);
	for (int i = 0; i < kStreams; ++i)
	{
		EXPECT_EQ(results[i], 0) << "stream " << i;
		EXPECT_EQ(texts[i], texts[0]) << "stream " << i << " diverged from stream 0";
	}
	EXPECT_NE(Lower(texts[0]).find("ask not"), std::string::npos) << "got: " << texts[0];

	const double audio_s = static_cast<double>(_samples.size()) / WHISPER_SAMPLE_RATE;
	std::printf("[  INFO    ] %d streams x %d rounds of %.1f s audio with %d threads each: %lld ms total (%.1fx real time per stream)\n",
				kStreams, kRounds, audio_s, threads, static_cast<long long>(elapsed_ms),
				(audio_s * kRounds) / (static_cast<double>(elapsed_ms) / 1000.0));

	for (auto *state : states)
	{
		_registry->DeleteState(state);
	}
}

// Freeing and allocating states while another state is mid-inference is the
// REST enable/disable path and new streams joining; the registry serializes
// init/free under its lock without stopping the inference.
TEST_F(WhisperRegistryModelTest, StateAllocFreeDuringInferenceIsStable)
{
	auto ctx = _registry->GetModelContext(Primary());
	ASSERT_NE(ctx, nullptr);

	auto *busy = _registry->NewState(Primary());
	ASSERT_NE(busy, nullptr);

	std::atomic<bool> inference_ok{true};
	std::thread inference([&] {
		for (int round = 0; round < 2; ++round)
		{
			if (whisper_full_with_state(ctx.get(), busy, DeterministicParams(_registry->GetThreadShare(0)), _samples.data(), static_cast<int>(_samples.size())) != 0)
			{
				inference_ok = false;
				return;
			}
		}
	});

	int cycles = 0;
	for (int i = 0; i < 20; ++i)
	{
		auto *state = _registry->NewState(Primary());
		if (state == nullptr)
		{
			break;
		}
		_registry->DeleteState(state);  // never ran: returns its pending reservation here
		cycles++;
	}
	inference.join();

	EXPECT_TRUE(inference_ok.load());
	EXPECT_EQ(cycles, 20);

	_registry->DeleteState(busy);
}

TEST_F(WhisperRegistryModelTest, ThreadShareFollowsLiveStates)
{
	ASSERT_NE(_registry->GetModelContext(Primary()), nullptr);

	_registry->SetMaxThreads(8);
	EXPECT_EQ(_registry->GetThreadShare(8), 8) << "no live states: the whole budget";

	std::vector<whisper_state *> states;
	for (int i = 0; i < 4; ++i)
	{
		auto *state = _registry->NewState(Primary());
		ASSERT_NE(state, nullptr);
		states.push_back(state);
	}
	EXPECT_EQ(_registry->GetThreadShare(8), 2) << "4 live states share 8 threads";
	EXPECT_EQ(_registry->GetThreadShare(1), 1) << "a smaller request is honoured";

	_registry->DeleteState(states.back());
	states.pop_back();
	EXPECT_EQ(_registry->GetThreadShare(8), 2) << "8 / 3 rounds down";

	for (auto *state : states)
	{
		_registry->DeleteState(state);
	}
	EXPECT_EQ(_registry->GetThreadShare(8), 8) << "all states gone: the whole budget is back";
}
