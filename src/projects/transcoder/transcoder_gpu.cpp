//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "transcoder_gpu.h"

#include "transcoder_private.h"

#ifdef HWACCELS_XMA_ENABLED
#include "xma.h"
#include "xrm.h"
#include "xrt.h"
#include "xrt/xrt_kernel.h"
#define MAX_XLNX_DEVS 128
#define XLNX_XCLBIN_PATH (char*)"/opt/xilinx/xcdr/xclbins/transcode.xclbin"
#define MAX_XLNX_DEVICES_PER_CMD 2
#endif

#ifdef HWACCELS_NVIDIA_ENABLED
#include <nvml.h>
#include <cuda.h>
#endif

TranscodeGPU::TranscodeGPU()
{
	for (int i = 0; i < MAX_DEVICE_COUNT; i++)
	{
		_device_context_qsv[i] = nullptr;
		_device_context_nilogan[i] = nullptr;
		_device_context_nv[i] = nullptr;
	}
	_initialized = false;
	_supported_devices.clear();
}

bool TranscodeGPU::Initialize()
{
	if (_initialized)
	{
		return true;
	}

	Uninitialize();

	logti("Trying to check available hardware accelerators");

	// CUDA
	if (CheckSupportedNV() == true)
	{
		logti("Supported NVIDIA accelerator. Number of devices(%d)", GetDeviceCount(cmn::MediaCodecModuleId::NVENC));
	}
	else
	{
		logtd("No supported NVIDIA accelerator");
	}

	// XMA
	if (CheckSupportedXMA() == true)
	{
		logti("Supported Xilinx Media accelerator. Number of devices(%d)", GetDeviceCount(cmn::MediaCodecModuleId::XMA));
	}
	else
	{
		logtd("No supported Xilinx Media accelerator");
	}

	// QSV
	if (CheckSupportedQSV() == true)
	{
		logti("Supported Intel QuickSync accelerator. Number of devices(%d)", GetDeviceCount(cmn::MediaCodecModuleId::QSV));
	}
	else
	{
		logtd("No supported Intel QuickSync accelerator");
	}

	// NILOGAN
	if (CheckSupportedNILOGAN() == true)
	{
		logti("Supported Netint VPU accelerator. Number of devices(%d)", GetDeviceCount(cmn::MediaCodecModuleId::NILOGAN));
	}
	else
	{
		logtd("No supported Netint VPU accelerator");
	}

	_initialized = true;

	return false;
}

bool TranscodeGPU::Uninitialize()
{
	if (!_initialized)
	{
		return true;
	}

	logti("Trying to release Transcoder GPU resources");

	for (int i = 0; i < MAX_DEVICE_COUNT; i++)
	{
		if (_device_context_nv[i] != nullptr)
		{
			av_buffer_unref(&_device_context_nv[i]);
			_device_context_nv[i] = nullptr;
		}

		if (_device_context_qsv[i] != nullptr)
		{
			av_buffer_unref(&_device_context_qsv[i]);
			_device_context_qsv[i] = nullptr;
		}

		if (_device_context_nilogan[i] != nullptr)
		{
			av_buffer_unref(&_device_context_nilogan[i]);
			_device_context_nilogan[i] = nullptr;
		}
	}

	_supported_devices.clear();
	_initialized = false;

#ifdef HWACCELS_NVIDIA_ENABLED
	nvmlShutdown();
#endif
	return true;
}

bool TranscodeGPU::IsSupported(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id)
{
	switch (id)
	{
		case cmn::MediaCodecModuleId::QSV:
			return IsSupportedQSV(gpu_id);
		case cmn::MediaCodecModuleId::NILOGAN:
			return IsSupportedNILOGAN(gpu_id);
		case cmn::MediaCodecModuleId::NVENC:
			return IsSupportedNV(gpu_id);
		case cmn::MediaCodecModuleId::XMA:
			return IsSupportedXMA(gpu_id);
		default:
			break;
	}

	return false;
}

int32_t TranscodeGPU::GetDeviceCount(cmn::MediaCodecModuleId id)
{
	switch (id)
	{
		case cmn::MediaCodecModuleId::QSV:
			return GetDeviceCountQSV();
		case cmn::MediaCodecModuleId::NILOGAN:
			return GetDeviceCountNILOGAN();
		case cmn::MediaCodecModuleId::NVENC:
			return GetDeviceCountNV();
		case cmn::MediaCodecModuleId::XMA:
			return GetDeviceCountXMA();
		default:
			break;
	}

	return 0;
}

AVBufferRef* TranscodeGPU::GetDeviceContext(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id)
{
	switch (id)
	{
		case cmn::MediaCodecModuleId::QSV:
			return GetDeviceContextQSV(gpu_id);
		case cmn::MediaCodecModuleId::NILOGAN:
			return GetDeviceContextNILOGAN(gpu_id);
		case cmn::MediaCodecModuleId::NVENC:
			return GetDeviceContextNV(gpu_id);
		case cmn::MediaCodecModuleId::XMA:
		default:
			break;
	}

	return nullptr;
}

int32_t TranscodeGPU::GetExternalDeviceId(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id)
{
	switch (id)
	{
		case cmn::MediaCodecModuleId::NVENC:
			return GetDeviceIdNV(gpu_id);
		default:
			break;
	}

	return -1;
}

ov::String TranscodeGPU::GetDeviceDisplayName(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id)
{
	if(gpu_id >= MAX_DEVICE_COUNT) {
		return "Unknown";
	}

	switch (id)
	{
		case cmn::MediaCodecModuleId::QSV:
			return _device_display_name_qsv[gpu_id];
		case cmn::MediaCodecModuleId::NILOGAN:
			return _device_display_name_nilogan[gpu_id];
		case cmn::MediaCodecModuleId::NVENC:
			return _device_display_name_nv[gpu_id];
		case cmn::MediaCodecModuleId::XMA:
			return _device_display_name_xma[gpu_id];
		default:
			break;
	}

	return "Unknown";
}

ov::String TranscodeGPU::GetDeviceBusId(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id)
{
	if(gpu_id >= MAX_DEVICE_COUNT) {
		return "Unknown";
	}

	switch (id)
	{
		case cmn::MediaCodecModuleId::NVENC:
			return _device_bus_id_nv[gpu_id];
		case cmn::MediaCodecModuleId::XMA:
			return _device_bus_id_xma[gpu_id];
		default:
			break;
	}

	return "Unknown";
}

int32_t TranscodeGPU::GetDeviceCountQSV()
{
	return _device_count_qsv;
}

int32_t TranscodeGPU::GetDeviceCountNILOGAN()
{
	return _device_count_nilogan;
}

int32_t TranscodeGPU::GetDeviceCountNV()
{
	return _device_count_nv;
}

int32_t TranscodeGPU::GetDeviceCountXMA()
{
	return _device_count_xma;
}

bool TranscodeGPU::CheckSupportedNV()
{
#ifdef HWACCELS_NVIDIA_ENABLED
	// Initialize NVML library
	nvmlReturn_t result = nvmlInit();
	if (result != NVML_SUCCESS)
	{
		logtt("NVML: Driver is not loaded or installed");
		return false;
	}

	// Initialize CUDA library
	if (cuInit(0) != CUDA_SUCCESS)
	{
		logte("Failed to initialize CUDA");
		return false;
	}

	// Get GPU device count
	unsigned int device_count;
	result = nvmlDeviceGetCount(&device_count);
	if (result != NVML_SUCCESS)
	{
		logte("Failed to get device count: %s", nvmlErrorString(result));
		return false;
	}

	_device_count_nv = 0;

	// Get GPU device handle
	for (unsigned int gpu_id = 0; gpu_id < device_count; gpu_id++)
	{
		nvmlDevice_t device;
		result = nvmlDeviceGetHandleByIndex(gpu_id, &device);
		if (result != NVML_SUCCESS)
		{
			logte("Failed to get device handle: %s", nvmlErrorString(result));
			continue;
		}

		// Get GPU name
		char device_name[NVML_DEVICE_NAME_BUFFER_SIZE];
		nvmlDeviceGetName(device, device_name, sizeof(device_name));

		// Get GPU PCI Bus ID
		nvmlPciInfo_t pci_info;
		nvmlDeviceGetPciInfo(device, &pci_info);

		// Get CUDA device index
		unsigned int cuda_index = -1;
		for (unsigned int j = 0; j < device_count; j++)
		{
			CUdevice cu_device;
			cuDeviceGet(&cu_device, j);

			int32_t cu_pci_bus_id;
			cuDeviceGetAttribute(&cu_pci_bus_id, CU_DEVICE_ATTRIBUTE_PCI_BUS_ID, cu_device);

			if (cu_pci_bus_id == (int32_t)pci_info.bus)
			{
				cuda_index = j;
				break;
			}
		}

		// Get HW device context
		char device_id[10];
		sprintf(device_id, "%d", cuda_index);
		int ret = ::av_hwdevice_ctx_create(&_device_context_nv[gpu_id], AV_HWDEVICE_TYPE_CUDA, device_id, nullptr, 1);
		if (ret < 0)
		{
			av_buffer_unref(&_device_context_nv[gpu_id]);
			_device_context_nv[gpu_id] = nullptr;

			logtw("No supported CUDA computing unit. [%s]", device_name);
		}
		else
		{
			_device_cuda_id_nv[gpu_id] = cuda_index;
			_device_display_name_nv[gpu_id] = device_name;
			_device_bus_id_nv[gpu_id] = pci_info.busId;
			_device_count_nv++;

			_supported_devices.push_back(std::make_pair(cmn::MediaCodecModuleId::NVENC, gpu_id));

			logti("NVIDIA. DeviceId(%d), Name(%s), BusId(%s), CudaId(%d)", gpu_id, device_name, pci_info.busId, cuda_index);
		}
	}

	if (_device_count_nv == 0)
	{
		return false;
	}

	return true;

#else
	_device_count_nv = 0;

	return false;
#endif
}

bool TranscodeGPU::CheckSupportedXMA()
{
#ifdef HWACCELS_XMA_ENABLED
	// Check XMA Daemon running
	xrmContext* xrm_ctx = (xrmContext*)xrmCreateContext(XRM_API_VERSION_1);
	if (xrm_ctx == NULL)
	{
		logtt("XMA: Driver is not loaded or installed");
		return false;
	}

	if (xrmIsDaemonRunning(xrm_ctx) == false)
	{
		logtw("XMA: Daemon is not running");
		return false;
	}

	if (xrmDestroyContext(xrm_ctx) != XRM_SUCCESS)
	{
		logte("XMA: Failed to destory context");
		return false;
	}

	// Enumerate Xilinx devices
	int32_t dev_id = 0;
	bool dev_list[MAX_XLNX_DEVS];
	XmaXclbinParameter xclbin_nparam[MAX_XLNX_DEVS];
	memset(dev_list, false, MAX_XLNX_DEVS * sizeof(bool));

	_device_count_xma = xma_num_devices();

	for (dev_id = 0; dev_id < _device_count_xma; dev_id++)
	{
		xclbin_nparam[dev_id].device_id = dev_id;
		xclbin_nparam[dev_id].xclbin_name = XLNX_XCLBIN_PATH;

		// Get device info
		xclDeviceHandle handle = xclOpen(dev_id, nullptr, XCL_INFO);
		if (!handle)
		{
			logte("Failed to open device %d", dev_id);
			continue;
		}

		xclDeviceInfo2 info;
		xclGetDeviceInfo2(handle, &info);
		int32_t pci_slot = static_cast<int32_t>(info.mPciSlot);
		_device_bus_id_xma[dev_id] = ov::String::FormatString("%02x:%02x.%d", (pci_slot >> 8) & 0xFF, pci_slot & 0xFF, dev_id);
		_device_display_name_xma[dev_id] = ov::String::FormatString("Xilinx Corporation Device %x", info.mDeviceId);		
		xclClose(handle);
		
		_supported_devices.push_back(std::make_pair(cmn::MediaCodecModuleId::XMA, dev_id));

		logti("XMA: deviceId(%d), Name(%s), BusId(%s), xclbin(%s)", xclbin_nparam[dev_id].device_id, _device_display_name_xma[dev_id].CStr(), _device_bus_id_xma[dev_id].CStr(), xclbin_nparam[dev_id].xclbin_name);
	}

	// Initialize all devices
	if (xma_initialize(xclbin_nparam, _device_count_xma) != 0)
	{
		logte("XMA: Failed to initialze");
		return false;
	}

	return true;
#else
	_device_count_xma = 0;

	return false;
#endif
}

bool TranscodeGPU::CheckSupportedQSV()
{
	_device_count_qsv = 0;

	int ret = ::av_hwdevice_ctx_create(&_device_context_qsv[0], AV_HWDEVICE_TYPE_QSV, nullptr, nullptr, 0);
	if (ret < 0)
	{
		av_buffer_unref(&_device_context_qsv[0]);
		_device_context_qsv[0] = nullptr;

		logtt("QSV: Driver is not loaded or installed");

		return false;
	}

	_device_count_qsv++;

	[[maybe_unused]]
	auto constraints = av_hwdevice_get_hwframe_constraints(_device_context_qsv[0], nullptr);

	logtt("constraints. hw.fmt(%d), sw.fmt(%d)", *constraints->valid_hw_formats, *constraints->valid_sw_formats);

	av_hwframe_constraints_free(&constraints);

	return true;
}

bool TranscodeGPU::CheckSupportedNILOGAN()
{
	_device_count_nilogan = 0;
#ifdef HWACCELS_NILOGAN_ENABLED
	int ret = ::av_hwdevice_ctx_create(&_device_context_nilogan[0], AV_HWDEVICE_TYPE_NI_LOGAN, nullptr, nullptr, 0);
	if (ret < 0)
	{
		av_buffer_unref(&_device_context_nilogan[0]);
		_device_context_nilogan[0] = nullptr;

		logtt("Netint: Driver is not loaded or installed");
		return false;
	}

	_device_count_nilogan++;

	auto constraints = av_hwdevice_get_hwframe_constraints(_device_context_nilogan[0], nullptr);

	logtt("constraints. hw.fmt(%d), sw.fmt(%d)", *constraints->valid_hw_formats, *constraints->valid_sw_formats);

	av_hwframe_constraints_free(&constraints);

	_supported_devices.push_back(std::make_pair(cmn::MediaCodecModuleId::NILOGAN, 0));

	return true;
#else
	return false;
#endif
}

AVBufferRef* TranscodeGPU::GetDeviceContextQSV(cmn::DeviceId gpu_id)
{
	if (gpu_id >= MAX_DEVICE_COUNT)
	{
		return nullptr;
	}

	return _device_context_qsv[gpu_id];
}

AVBufferRef* TranscodeGPU::GetDeviceContextNILOGAN(cmn::DeviceId gpu_id)
{
	if (gpu_id >= MAX_DEVICE_COUNT)
	{
		return nullptr;
	}

	return _device_context_nilogan[gpu_id];
}

AVBufferRef* TranscodeGPU::GetDeviceContextNV(cmn::DeviceId gpu_id)
{
	if (gpu_id >= MAX_DEVICE_COUNT)
	{
		return nullptr;
	}

	return _device_context_nv[gpu_id];
}

int32_t TranscodeGPU::GetDeviceIdNV(cmn::DeviceId gpu_id)
{
	if (gpu_id >= MAX_DEVICE_COUNT)
	{
		return -1;
	}

	return _device_cuda_id_nv[gpu_id];
}

bool TranscodeGPU::IsSupportedQSV(cmn::DeviceId gpu_id)
{
	if (_device_count_qsv == 0 || gpu_id >= _device_count_qsv)
	{
		return false;
	}

	return true;
}

bool TranscodeGPU::IsSupportedNILOGAN(cmn::DeviceId gpu_id)
{
	if (_device_count_nilogan == 0 || gpu_id >= _device_count_nilogan)
	{
		return false;
	}

	return true;
}

bool TranscodeGPU::IsSupportedNV(cmn::DeviceId gpu_id)
{
	if (_device_count_nv == 0 || gpu_id >= _device_count_nv)
	{
		return false;
	}

	return true;
}

bool TranscodeGPU::IsSupportedXMA(cmn::DeviceId gpu_id)
{
	if (_device_count_xma == 0 || gpu_id >= _device_count_xma)
	{
		return false;
	}

	return true;
}

uint32_t TranscodeGPU::GetUtilization(IPType type, cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id)
{
	switch (id)
	{
		case cmn::MediaCodecModuleId::XMA: {
#ifdef HWACCELS_XMA_ENABLED
			if (type == IPType::ENCODER)
			{
				return 0;
			}
			else if (type == IPType::DECODER)
			{
				return 0;
			}
			else if (type == IPType::SCALER)
			{
				return 0;
			}
#endif
		}
		break;
		case cmn::MediaCodecModuleId::NVENC: {
#ifdef HWACCELS_NVIDIA_ENABLED
			nvmlDevice_t device;
			nvmlReturn_t result = nvmlDeviceGetHandleByIndex(gpu_id, &device);
			if (result != NVML_SUCCESS)
			{
				logte("Failed to get device handle: %s", nvmlErrorString(result));
				return 0;
			}

			if (type == IPType::ENCODER)
			{
				uint32_t encUtilization;
				uint32_t encSamplingPeriod;
				nvmlDeviceGetEncoderUtilization(device, &encUtilization, &encSamplingPeriod);

				return encUtilization;
			}
			else if (type == IPType::DECODER)
			{
				uint32_t decUtilization;
				uint32_t decSamplingPeriod;
				nvmlDeviceGetDecoderUtilization(device, &decUtilization, &decSamplingPeriod);

				return decUtilization;
			}
			else if (type == IPType::SCALER)
			{
				nvmlUtilization_st device_utilization;
				nvmlDeviceGetUtilizationRates(device, &device_utilization);

				return device_utilization.gpu;
			}
#endif
			// logti("GpuId(%d), Name(%40s), dec.utilization(%d%%), enc.utilization(%d%%)", gpu_id, name, decUtilization, encUtilization);
		}
		break;
		case cmn::MediaCodecModuleId::QSV: {
			if (type == IPType::ENCODER)
			{
				return 0;
			}
			else if (type == IPType::DECODER)
			{
				return 0;
			}
			else if (type == IPType::SCALER)
			{
				return 0;
			}
		}
		break;
		case cmn::MediaCodecModuleId::NILOGAN: {
#ifdef HWACCELS_NILOGAN_ENABLED
			if (type == IPType::ENCODER)
			{
				return 0;
			}
			else if (type == IPType::DECODER)
			{
				return 0;
			}
			else if (type == IPType::SCALER)
			{
				return 0;
			}
#endif
		}
		break;
		default:
			break;
	}

	return 0;
}

void TranscodeGPU::CodecThread()
{
	ov::logger::ThreadHelper thread_helper;

	std::vector<cmn::MediaCodecModuleId> modules;

	modules.push_back(cmn::MediaCodecModuleId::NVENC);
	modules.push_back(cmn::MediaCodecModuleId::XMA);
	modules.push_back(cmn::MediaCodecModuleId::QSV);
	modules.push_back(cmn::MediaCodecModuleId::NILOGAN);

	while(true)
	{
		for (auto module : modules)
		{
			for (int gpu_id = 0; gpu_id < GetDeviceCount(module); gpu_id++)
			{
				logti("[%s:%d] dec:%d%%, enc:%d%%, scaler:%d%%",
					cmn::GetCodecModuleIdString(module),
					gpu_id,
					GetUtilization(IPType::DECODER, module, gpu_id),
					GetUtilization(IPType::ENCODER, module, gpu_id),
					GetUtilization(IPType::SCALER, module, gpu_id));
			}
		}
		// Every 10 seconds
		std::this_thread::sleep_for(std::chrono::seconds(4));		
	}
	
}
