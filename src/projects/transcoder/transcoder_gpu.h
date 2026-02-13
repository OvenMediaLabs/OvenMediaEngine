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

#include "codec/codec_base.h"

#define MAX_DEVICE_COUNT 16


class TranscodeGPU : public ov::Singleton<TranscodeGPU>
{

public:
	TranscodeGPU();

	bool Initialize();
	bool Uninitialize();

	bool IsSupported(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

	int32_t GetDeviceCount(cmn::MediaCodecModuleId id);

	AVBufferRef *GetDeviceContext(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

	int32_t GetExternalDeviceId(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);
	
	ov::String GetDeviceDisplayName(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

	ov::String GetDeviceBusId(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

	enum class IPType : int32_t
	{
		DECODER = 0,
		ENCODER,
		SCALER,
	};

	uint32_t GetUtilization(IPType type, cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id = 0);

protected:
	bool CheckSupportedQSV();
	bool CheckSupportedNILOGAN();
	bool CheckSupportedNV();
	bool CheckSupportedXMA();

	AVBufferRef *GetDeviceContextQSV(cmn::DeviceId gpu_id = 0);
	AVBufferRef *GetDeviceContextNILOGAN(cmn::DeviceId gpu_id = 0);
	AVBufferRef *GetDeviceContextNV(cmn::DeviceId gpu_id = 0);

	bool IsSupportedQSV(cmn::DeviceId gpu_id = 0);
	bool IsSupportedNILOGAN(cmn::DeviceId gpu_id = 0);
	bool IsSupportedNV(cmn::DeviceId gpu_id = 0);
	bool IsSupportedXMA(cmn::DeviceId gpu_id = 0);

	cmn::DeviceId GetDeviceCountQSV();
	cmn::DeviceId GetDeviceCountNILOGAN();
	cmn::DeviceId GetDeviceCountNV();
	cmn::DeviceId GetDeviceCountXMA();

	int32_t GetDeviceIdNV(cmn::DeviceId gpu_id = 0);

	std::atomic<bool> _initialized{false};

	std::vector<std::pair<cmn::MediaCodecModuleId, cmn::DeviceId>> _supported_devices;

	int32_t _device_count_xma;
	ov::String _device_display_name_xma[MAX_DEVICE_COUNT];
	ov::String _device_bus_id_xma[MAX_DEVICE_COUNT];
	
	AVBufferRef *_device_context_qsv[MAX_DEVICE_COUNT];
	ov::String _device_display_name_qsv[MAX_DEVICE_COUNT];
	int32_t _device_count_qsv;

	AVBufferRef *_device_context_nilogan[MAX_DEVICE_COUNT];
	ov::String _device_display_name_nilogan[MAX_DEVICE_COUNT];
	int32_t _device_count_nilogan;

	AVBufferRef *_device_context_nv[MAX_DEVICE_COUNT];
	ov::String _device_display_name_nv[MAX_DEVICE_COUNT];
	ov::String _device_bus_id_nv[MAX_DEVICE_COUNT];
	int32_t _device_cuda_id_nv[MAX_DEVICE_COUNT];
	int32_t _device_count_nv;

	void CodecThread();
	std::thread _thread;

public:
	std::mutex& GetDeviceMutex() {
		return _device_mutex;
	}
protected:
	// Global synchronization for specific hardware (Xilinx U30)
	std::mutex _device_mutex;
};