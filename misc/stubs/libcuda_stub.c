#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define CUDAAPI
typedef void* CUdevice;
typedef void* CUdevice_attribute;
typedef enum cudaError_enum { stub } CUresult;

typedef unsigned long long CUdeviceptr;
typedef unsigned long long CUmemGenericAllocationHandle;
typedef int CUmemAllocationGranularity_flags;

CUresult CUDAAPI cuDeviceGet(CUdevice *device, int ordinal)
{
	return 0;
}

CUresult CUDAAPI cuInit(unsigned int Flags)
{
	return 0;
}

CUresult CUDAAPI cuDeviceGetAttribute(int *pi, CUdevice_attribute attrib, CUdevice dev)
{
	return 0;
}

CUresult CUDAAPI cuDeviceGetCount(int *count)
{
	return 0;
}

CUresult CUDAAPI cuGetErrorString(CUresult error, const char **pStr)
{
	if (pStr) *pStr = "_stub_";
	return 0;
}

CUresult CUDAAPI cuMemAddressReserve(CUdeviceptr *ptr, size_t size, size_t alignment, CUdeviceptr addr, unsigned long long flags)
{
	return 0;
}

CUresult CUDAAPI cuMemAddressFree(CUdeviceptr ptr, size_t size)
{
	return 0;
}

CUresult CUDAAPI cuMemCreate(CUmemGenericAllocationHandle *handle, size_t size, const void *prop, unsigned long long flags)
{
	return 0;
}

CUresult CUDAAPI cuMemRelease(CUmemGenericAllocationHandle handle)
{
	return 0;
}

CUresult CUDAAPI cuMemMap(CUdeviceptr ptr, size_t size, size_t offset, CUmemGenericAllocationHandle handle, unsigned long long flags)
{
	return 0;
}

CUresult CUDAAPI cuMemUnmap(CUdeviceptr ptr, size_t size)
{
	return 0;
}

CUresult CUDAAPI cuMemSetAccess(CUdeviceptr ptr, size_t size, const void *desc, size_t count)
{
	return 0;
}

CUresult CUDAAPI cuMemGetAllocationGranularity(size_t *granularity, const void *prop, CUmemAllocationGranularity_flags option)
{
	if (granularity) *granularity = 0;
	return 0;
}
