//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "transcoder_gpu.h"

#include <modules/ffmpeg/compat.h>

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
		_device_context_nilogan[i].reset();
		_device_context_nv[i].reset();
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
		_device_context_nv[i].reset();
		_device_context_nilogan[i].reset();
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

std::shared_ptr<HwDeviceContext> TranscodeGPU::GetDeviceContext(cmn::MediaCodecModuleId id, cmn::DeviceId gpu_id)
{
	switch (id)
	{
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
	
	_device_count_nv = 0;

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
	unsigned int nvml_device_count;
	result = nvmlDeviceGetCount(&nvml_device_count);
	if (result != NVML_SUCCESS)
	{
		logte("Failed to get device count: %s", nvmlErrorString(result));
		return false;
	}

	// Get CUDA device count
	int cuda_count = 0;
	if (cuDeviceGetCount(&cuda_count) != CUDA_SUCCESS)
	{
		logte("Failed to get CUDA device count");
		return false;
	}
	logtd("NVML:Found %d GPU(s), CUDA: Found %d GPU(s)", nvml_device_count, cuda_count);

	// Match NVML devices with CUDA devices using PCI attributes and create HW device contexts
	for (unsigned int device_id = 0; device_id < nvml_device_count; device_id++)
	{
		// Get nvml device handle
		nvmlDevice_t device;
		result = nvmlDeviceGetHandleByIndex(device_id, &device);
		if (result != NVML_SUCCESS)
		{
			logte("Failed to get device handle: %s", nvmlErrorString(result));
			continue;
		}

		// Get nvml device name
		char device_name[NVML_DEVICE_NAME_BUFFER_SIZE];
		nvmlDeviceGetName(device, device_name, sizeof(device_name));

		// Get nvml device PCI attributes (domain, bus, device)
		nvmlPciInfo_t pci_info;
		nvmlDeviceGetPciInfo(device, &pci_info);
		logtd("NVML Device %d: Name(%s), DomainId(%d), BusId(%d), DeviceId(%d)", device_id, device_name, pci_info.domain, pci_info.bus, pci_info.device);

		// Matching CUDA device based on nvml device PCI attributes
		int32_t matched_cu_index = -1;
		for (int32_t cu_index = 0; cu_index < cuda_count; cu_index++)
		{
			// Get CUDA device handle
			CUdevice cu_device;
			if (cuDeviceGet(&cu_device, cu_index) != CUDA_SUCCESS)
			{
				continue;
			}

			// Get CUDA device PCI attributes
			int32_t cu_pci_domain_id = -1;
			int32_t cu_pci_bus_id	 = -1;
			int32_t cu_pci_device_id = -1;

			if (cuDeviceGetAttribute(&cu_pci_domain_id, CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID, cu_device) != CUDA_SUCCESS ||
				cuDeviceGetAttribute(&cu_pci_bus_id, CU_DEVICE_ATTRIBUTE_PCI_BUS_ID, cu_device) != CUDA_SUCCESS ||
				cuDeviceGetAttribute(&cu_pci_device_id, CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID, cu_device) != CUDA_SUCCESS)
			{
				logtw("Failed to get PCI attributes for CUDA device %d", cu_index);
				continue;
			}
			
			logtd("CUDA Device %d: DomainId(%d), BusId(%d), DeviceId(%d)", cu_index, cu_pci_domain_id, cu_pci_bus_id, cu_pci_device_id);

			// Compare PCI attributes to find matching CUDA device for NVML device
			if (cu_pci_domain_id == (int32_t)pci_info.domain &&
				cu_pci_bus_id == (int32_t)pci_info.bus &&
				cu_pci_device_id == (int32_t)pci_info.device)
			{
				matched_cu_index = cu_index;
				break;
			}
		}

		// If no matching CUDA device found, skip this NVML device
		if (matched_cu_index < 0)
		{
			logtw("Failed to map NVML device to CUDA device. Name(%s), DomainId(%d), BusId(%d), DeviceId(%d)",
				  device_name, pci_info.domain, pci_info.bus, pci_info.device);
			continue;
		}

		// Create CUDA device context
		_device_context_nv[device_id] = ffmpeg::FFmpegHwDeviceContext::Create(cmn::MediaCodecModuleId::NVENC, ov::String::FormatString("%d", matched_cu_index));
		if (_device_context_nv[device_id] == nullptr)
		{
			logtw("Failed to create CUDA device context for device %d (CUDA index %d)", device_id, matched_cu_index);
			continue;
		}

		_device_cuda_id_nv[device_id]	   = matched_cu_index;
		_device_display_name_nv[device_id] = device_name;
		_device_bus_id_nv[device_id]	   = pci_info.busId;  // 00000000:00:04.0 (domain:bus:device.function)

		_device_count_nv++;

		_supported_devices.push_back(std::make_pair(cmn::MediaCodecModuleId::NVENC, device_id));

		logti("NVIDIA. DeviceId(%d), Name(%s), BusId(%s), CudaId(%d)", device_id, device_name, pci_info.busId, matched_cu_index);
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

bool TranscodeGPU::CheckSupportedNILOGAN()
{
	_device_count_nilogan = 0;
#ifdef HWACCELS_NILOGAN_ENABLED
	_device_context_nilogan[0] = ffmpeg::FFmpegHwDeviceContext::Create(cmn::MediaCodecModuleId::NILOGAN);
	if (_device_context_nilogan[0] == nullptr)
	{
		logtt("Netint: Driver is not loaded or installed");
		return false;
	}

	_device_count_nilogan++;

	_supported_devices.push_back(std::make_pair(cmn::MediaCodecModuleId::NILOGAN, 0));

	return true;
#else
	return false;
#endif
}

std::shared_ptr<HwDeviceContext> TranscodeGPU::GetDeviceContextNILOGAN(cmn::DeviceId gpu_id)
{
	if (gpu_id >= MAX_DEVICE_COUNT)
	{
		return nullptr;
	}

	return _device_context_nilogan[gpu_id];
}

std::shared_ptr<HwDeviceContext> TranscodeGPU::GetDeviceContextNV(cmn::DeviceId gpu_id)
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
