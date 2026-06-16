//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/mediarouter/media_type.h>

#include "hw_device_context.h"

#define MAX_DEVICE_COUNT 16


class TranscodeGPU : public ov::Singleton<TranscodeGPU>
{

public:
	TranscodeGPU();

	bool Initialize();
	bool Uninitialize();

	bool IsSupported(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

	int32_t GetDeviceCount(cmn::MediaCodecModuleId id);

	std::shared_ptr<HwDeviceContext> GetDeviceContext(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

	int32_t GetExternalDeviceId(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);
	
	ov::String GetDeviceDisplayName(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

	ov::String GetDeviceBusId(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

protected:
	bool CheckSupportedNILOGAN();
	bool CheckSupportedNV();
	bool CheckSupportedXMA();

	std::shared_ptr<HwDeviceContext> GetDeviceContextNILOGAN(cmn::DeviceId gpu_id = 0);
	std::shared_ptr<HwDeviceContext> GetDeviceContextNV(cmn::DeviceId gpu_id = 0);

	bool IsSupportedNILOGAN(cmn::DeviceId gpu_id = 0);
	bool IsSupportedNV(cmn::DeviceId gpu_id = 0);
	bool IsSupportedXMA(cmn::DeviceId gpu_id = 0);

	int32_t GetDeviceCountNILOGAN();
	int32_t GetDeviceCountNV();
	int32_t GetDeviceCountXMA();

	int32_t GetDeviceIdNV(cmn::DeviceId gpu_id = 0);

	std::atomic<bool> _initialized{false};

	std::vector<std::pair<cmn::MediaCodecModuleId, cmn::DeviceId>> _supported_devices;

	int32_t _device_count_xma;
	ov::String _device_display_name_xma[MAX_DEVICE_COUNT];
	ov::String _device_bus_id_xma[MAX_DEVICE_COUNT];
	
	std::shared_ptr<HwDeviceContext> _device_context_nilogan[MAX_DEVICE_COUNT];
	ov::String _device_display_name_nilogan[MAX_DEVICE_COUNT];
	int32_t _device_count_nilogan;

	std::shared_ptr<HwDeviceContext> _device_context_nv[MAX_DEVICE_COUNT];
	ov::String _device_display_name_nv[MAX_DEVICE_COUNT];
	ov::String _device_bus_id_nv[MAX_DEVICE_COUNT];
	int32_t _device_cuda_id_nv[MAX_DEVICE_COUNT];
	int32_t _device_count_nv;

public:
	ov::Mutex& GetDeviceMutex() {
		return _device_mutex;
	}
protected:
	// Global synchronization for specific hardware (Xilinx U30)
	ov::Mutex _device_mutex;
};