#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define CUDAAPI
typedef void* CUdevice;
typedef void* CUdevice_attribute;
typedef enum cudaError_enum {
	CUDA_SUCCESS                 = 0,
	CUDA_ERROR_STUB_LIBRARY      = 34,
} CUresult;

typedef unsigned long long CUdeviceptr;
typedef unsigned long long CUmemGenericAllocationHandle;
typedef int CUmemAllocationGranularity_flags;

CUresult CUDAAPI cuDeviceGet(CUdevice *device, int ordinal)
{
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuInit(unsigned int Flags)
{
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuDeviceGetAttribute(int *pi, CUdevice_attribute attrib, CUdevice dev)
{
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuDeviceGetCount(int *count)
{
	if (count) *count = 0;
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuGetErrorString(CUresult error, const char **pStr)
{
	if (pStr) *pStr = "CUDA stub library: driver not available";
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuMemAddressReserve(CUdeviceptr *ptr, size_t size, size_t alignment, CUdeviceptr addr, unsigned long long flags)
{
	if (ptr) *ptr = 0;
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuMemAddressFree(CUdeviceptr ptr, size_t size)
{
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuMemCreate(CUmemGenericAllocationHandle *handle, size_t size, const void *prop, unsigned long long flags)
{
	if (handle) *handle = 0;
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuMemRelease(CUmemGenericAllocationHandle handle)
{
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuMemMap(CUdeviceptr ptr, size_t size, size_t offset, CUmemGenericAllocationHandle handle, unsigned long long flags)
{
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuMemUnmap(CUdeviceptr ptr, size_t size)
{
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuMemSetAccess(CUdeviceptr ptr, size_t size, const void *desc, size_t count)
{
	return CUDA_ERROR_STUB_LIBRARY;
}

CUresult CUDAAPI cuMemGetAllocationGranularity(size_t *granularity, const void *prop, CUmemAllocationGranularity_flags option)
{
	if (granularity) *granularity = 0;
	return CUDA_ERROR_STUB_LIBRARY;
}
