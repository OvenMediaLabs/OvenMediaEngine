#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

	typedef int cudaError_t;
#define cudaSuccess 0
#define cudaErrorNotSupported 801

	cudaError_t cudaGetDeviceCount(int* count)
	{
		if (count)
			*count = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaGetDevice(int* d)
	{
		if (d)
			*d = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaSetDevice(int d)
	{
		(void)d;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaMalloc(void** p, size_t sz)
	{
		(void)sz;
		if (p)
			*p = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaMallocHost(void** p, size_t sz)
	{
		(void)sz;
		if (p)
			*p = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaMallocManaged(void** p, size_t sz, unsigned int flags)
	{
		(void)sz;
		(void)flags;
		if (p)
			*p = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaFree(void* p)
	{
		(void)p;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaFreeHost(void* p)
	{
		(void)p;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaMemcpyAsync(void* d, const void* s, size_t n, int kind,
								void* stream)
	{
		(void)d;
		(void)s;
		(void)n;
		(void)kind;
		(void)stream;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaMemcpy2DAsync(void* d, size_t dp, const void* s, size_t sp,
								  size_t w, size_t h, int kind, void* stream)
	{
		(void)d;
		(void)dp;
		(void)s;
		(void)sp;
		(void)w;
		(void)h;
		(void)kind;
		(void)stream;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaMemcpy3DPeerAsync(const void* p, void* stream)
	{
		(void)p;
		(void)stream;
		return cudaErrorNotSupported;
	}  
	cudaError_t cudaMemcpyPeerAsync(void* dst, int dstDev, const void* src,
									int srcDev, size_t count, void* stream)
	{
		(void)dst;
		(void)dstDev;
		(void)src;
		(void)srcDev;
		(void)count;
		(void)stream;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaMemset(void* d, int v, size_t n)
	{
		(void)d;
		(void)v;
		(void)n;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaMemsetAsync(void* d, int v, size_t n, void* stream)
	{
		(void)d;
		(void)v;
		(void)n;
		(void)stream;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaMemGetInfo(size_t* freeB, size_t* totalB)
	{
		if (freeB)
			*freeB = 0;
		if (totalB)
			*totalB = 0;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaDeviceSynchronize(void)
	{
		return cudaErrorNotSupported;
	}
	cudaError_t cudaDeviceGetAttribute(int* v, int attr, int dev)
	{
		(void)attr;
		(void)dev;
		if (v)
			*v = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaDeviceCanAccessPeer(int* can, int d, int p)
	{
		(void)d;
		(void)p;
		if (can)
			*can = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaDeviceEnablePeerAccess(int p, unsigned int f)
	{
		(void)p;
		(void)f;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaDeviceDisablePeerAccess(int p)
	{
		(void)p;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaGetLastError(void)
	{
		return cudaErrorNotSupported;
	}
	const char* cudaGetErrorString(cudaError_t e)
	{
		(void)e;
		return "cuda stub: not supported";
	}

	typedef struct
	{
		int dummy;
	} cudaDeviceProp;  
	cudaError_t cudaGetDeviceProperties(cudaDeviceProp* p, int d)
	{
		(void)d;
		if (p)
			p->dummy = 0;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaEventCreateWithFlags(void** e, unsigned int f)
	{
		(void)f;
		if (e)
			*e = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaEventRecord(void* e, void* s)
	{
		(void)e;
		(void)s;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaEventSynchronize(void* e)
	{
		(void)e;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaEventDestroy(void* e)
	{
		(void)e;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaStreamCreateWithFlags(void** s, unsigned int f)
	{
		(void)f;
		if (s)
			*s = 0;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaStreamDestroy(void* s)
	{
		(void)s;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaStreamSynchronize(void* s)
	{
		(void)s;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaStreamWaitEvent(void* s, void* e, unsigned int f)
	{
		(void)s;
		(void)e;
		(void)f;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaFuncSetAttribute(const void* fptr, int attr, int val)
	{
		(void)fptr;
		(void)attr;
		(void)val;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaFuncGetAttributes(void* attr, const void* fptr)
	{
		(void)attr;
		(void)fptr;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(
		int* num, const void* fptr, int bs, size_t dynSmem, int flags)
	{
		(void)fptr;
		(void)bs;
		(void)dynSmem;
		(void)flags;
		if (num)
			*num = 0;
		return cudaErrorNotSupported;
	}

 	typedef struct
	{
		unsigned int x, y, z;
	} dim3;

	cudaError_t cudaLaunchKernel(const void* fptr, dim3 grid, dim3 block,
								 void** args, size_t shmem, void* stream)
	{
		(void)fptr;
		(void)grid;
		(void)block;
		(void)args;
		(void)shmem;
		(void)stream;
		return cudaErrorNotSupported;
	}

	cudaError_t cudaHostRegister(void* p, size_t sz, unsigned int flags)
	{
		(void)p;
		(void)sz;
		(void)flags;
		return cudaErrorNotSupported;
	}
	cudaError_t cudaHostUnregister(void* p)
	{
		(void)p;
		return cudaErrorNotSupported;
	}
	cudaError_t __cudaPushCallConfiguration(dim3 grid, dim3 block, size_t shmem,
											void* stream)
	{
		(void)grid;
		(void)block;
		(void)shmem;
		(void)stream;
		return cudaErrorNotSupported;
	}
	cudaError_t __cudaPopCallConfiguration(dim3* grid, dim3* block, size_t* shmem,
										   void** stream)
	{
		(void)grid;
		(void)block;
		(void)shmem;
		(void)stream;
		return cudaErrorNotSupported;
	}

	void __cudaRegisterFunction(void) {}
	void __cudaRegisterVar(void) {}
	void __cudaRegisterFatBinary(void) {}
	void __cudaUnregisterFatBinary(void) {}
	void __cudaRegisterFatBinaryEnd(void) {}

	cudaError_t cudaPeekAtLastError(void)
	{
		return cudaErrorNotSupported;
	}

#ifdef __cplusplus
}
#endif