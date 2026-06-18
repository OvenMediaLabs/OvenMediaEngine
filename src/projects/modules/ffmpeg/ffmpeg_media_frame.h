//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#pragma once

#include <cstring>
#include <memory>

extern "C"
{
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

#include <transcoder/media_frame.h>

namespace ffmpeg
{
	class FFmpegMediaFrameData : public MediaFrameData
	{
	public:
		// Takes ownership of the given AVFrame.
		explicit FFmpegMediaFrameData(AVFrame *frame);
		~FFmpegMediaFrameData() override;

		Backend GetBackend() const override;
		void *GetNativeHandle() const override;
		
		std::shared_ptr<MediaFrameData> Clone(bool deep) const override;
		bool IsHardwareFrame() const override;
		std::shared_ptr<MediaFrameData> DownloadToHost() const override;
		void FillZero() override;
		void SetPts(int64_t pts) override;
		void SetDuration(int64_t duration) override;
		void SetNbSamples(int32_t nb_samples) override;

	private:
		AVFrame *_frame = nullptr;
	};
}  // namespace ffmpeg
