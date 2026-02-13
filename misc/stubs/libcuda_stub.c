#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define CUDAAPI
typedef void* CUdevice;
typedef void* CUdevice_attribute;
typedef enum cudaError_enum { stub } CUresult;

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