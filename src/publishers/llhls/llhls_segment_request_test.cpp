#include <gtest/gtest.h>

#include <deque>
#include <mutex>
#include <optional>

#include <base/common_types.h>
#include <base/info/dump.h>
#include <base/publisher/stream.h>
#include <modules/containers/bmff/fmp4_packager/duration_boundary_policy.h>
#include <modules/containers/bmff/fmp4_packager/fmp4_packager.h>
#include <modules/containers/bmff/fmp4_packager/synced_boundary_policy.h>
#include <modules/containers/webvtt/webvtt_packager.h>
#include <modules/dump/dump.h>
#include <monitoring/monitoring.h>
#include "llhls_chunklist.h"
#include "llhls_keyframe_start_aligner.h"
#include "llhls_master_playlist.h"

// Their dependencies are included above, so only this header sees the redefinition
#define private public
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
}  // namespace

TEST(LLHlsSegmentRequest, InProgressSegmentIsNotFoundUntilCompleted)
{
	const auto &app_info = TestApplicationInfo::GetInvalidApplication();
	auto app			 = std::make_shared<LLHlsApplication>(nullptr, app_info);
	auto stream			 = std::make_shared<LLHlsStream>(app, info::Stream(app_info, StreamSourceType::Ovt), false, 0);

	auto track = std::make_shared<MediaTrack>();
	track->SetId(1);
	track->SetMediaType(cmn::MediaType::Video);
	track->SetTimeBase(1, 1000);

	bmff::SegmentBoundaryPolicy::Config policy_config;
	auto storage = std::make_shared<bmff::FMP4Storage>(nullptr, track, bmff::FMP4Storage::Config{}, "test",
													   std::make_shared<bmff::DurationBoundaryPolicy>(policy_config, false));
	stream->_storage_map[1] = storage;

	auto media = ov::String("media").ToData(false);
	ASSERT_TRUE(storage->AppendMediaChunk(media, 0, 500, true, false));
	auto segment_number = storage->GetLastSegmentNumber();

	auto [in_progress, in_progress_data] = stream->GetSegment(1, segment_number);
	EXPECT_EQ(in_progress, LLHlsStream::RequestResult::NotFound);
	EXPECT_EQ(in_progress_data, nullptr);

	ASSERT_TRUE(storage->AppendMediaChunk(media, 500, 500, true, true));

	auto [completed, completed_data] = stream->GetSegment(1, segment_number);
	EXPECT_EQ(completed, LLHlsStream::RequestResult::Success);
	ASSERT_NE(completed_data, nullptr);
	EXPECT_EQ(completed_data->GetLength(), media->GetLength() * 2);
}
