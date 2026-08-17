#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define nullptr ((void*)0)
#define DECLDIR

typedef void* CUdevice;
typedef void* CUdevice_attribute;
typedef enum nvmlReturn_enum {  
	NVML_SUCCESS = 0,                   //!< The operation was successful
    NVML_ERROR_UNINITIALIZED = 1,       //!< NVML was not first initialized with nvmlInit()
	 } nvmlReturn_t;;
typedef struct nvmlDevice_st* nvmlDevice_t;
typedef struct nvmlUtilization_st* nvmlUtilization_t;
typedef struct nvmlPciInfo_st* nvmlPciInfo_t;

#define nvmlInit                    nvmlInit_v2
#define nvmlDeviceGetPciInfo        nvmlDeviceGetPciInfo_v3
#define nvmlDeviceGetCount          nvmlDeviceGetCount_v2
#define nvmlDeviceGetHandleByIndex  nvmlDeviceGetHandleByIndex_v2
nvmlReturn_t DECLDIR nvmlInit(void)
{
	return NVML_ERROR_UNINITIALIZED;
}

nvmlReturn_t DECLDIR nvmlDeviceGetUtilizationRates(nvmlDevice_t device, nvmlUtilization_t *utilization)
{
	return NVML_ERROR_UNINITIALIZED;
}

nvmlReturn_t DECLDIR nvmlDeviceGetEncoderUtilization(nvmlDevice_t device, unsigned int *utilization, unsigned int *samplingPeriodUs)
{
	return NVML_ERROR_UNINITIALIZED;
}

nvmlReturn_t DECLDIR nvmlDeviceGetDecoderUtilization(nvmlDevice_t device, unsigned int *utilization, unsigned int *samplingPeriodUs)
{
	return NVML_ERROR_UNINITIALIZED;
}

nvmlReturn_t DECLDIR nvmlDeviceGetName(nvmlDevice_t device, char *name, unsigned int length)
{
	return NVML_ERROR_UNINITIALIZED;
}

nvmlReturn_t DECLDIR nvmlDeviceGetPciInfo(nvmlDevice_t device, nvmlPciInfo_t *pci)
{
	return NVML_ERROR_UNINITIALIZED;
}

nvmlReturn_t DECLDIR nvmlDeviceGetCount(unsigned int *deviceCount)
{
	return NVML_ERROR_UNINITIALIZED;
}

const DECLDIR char* nvmlErrorString(nvmlReturn_t result)
{
	return "_stub_";
}

nvmlReturn_t DECLDIR nvmlShutdown(void)
{
	return NVML_ERROR_UNINITIALIZED;
}

nvmlReturn_t DECLDIR nvmlDeviceGetHandleByIndex(unsigned int index, nvmlDevice_t *device)
{
	return NVML_ERROR_UNINITIALIZED;
}