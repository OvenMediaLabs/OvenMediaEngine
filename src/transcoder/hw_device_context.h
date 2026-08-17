//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

//  HwDeviceContext
//
//  Backend-agnostic handle for a hardware (GPU/VPU) device context.
//
//  TranscodeGPU manages hardware accelerators and hands out an opaque device
//  context through this interface, so that transcoder_gpu.h stays free of any
//  codec-library dependency 

//  Each backend provides its own implementation:
//   - ffmpeg::FFmpegHwDeviceContext   (wraps AVBufferRef)
//   - nvidia::NvidiaHwDeviceContext   (wraps NVML/CUDA context, planned)
//   - xma::XmaHwDeviceContext         (wraps XMA context, planned)
//   - netint::NetintHwDeviceContext   (wraps libxcoder device, planned)
#pragma once

#include <base/mediarouter/media_type.h>

class HwDeviceContext
{
public:
	virtual ~HwDeviceContext() = default;

	// Identifies which accelerator/backend owns the native handle returned by
	// GetNativeHandle().
	virtual cmn::MediaCodecModuleId GetModuleId() const = 0;

	// Returns the raw backend-specific handle.
	virtual void *GetNativeHandle() const = 0;
};
