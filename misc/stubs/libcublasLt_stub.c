// libcublaslt_stub.c
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal cuBLASLt stub to satisfy dynamic linker on systems without NVIDIA
   drivers. All functions return CUBLAS_STATUS_NOT_SUPPORTED.  Types are reduced
   to opaque void* or int. */

typedef int cublasStatus_t;
typedef int cublasComputeType_t;
typedef int cudaDataType;
typedef void* cudaStream_t;

/* Opaque handle/types */
typedef void* cublasLtHandle_t;
typedef void* cublasLtMatmulDesc_t;
typedef void* cublasLtMatrixLayout_t;
typedef void* cublasLtMatmulPreference_t;
typedef void* cublasLtMatmulAlgo_t;

/* Heuristic result is a struct in real API; keep a tiny placeholder to match
 * by-name linking */
typedef struct 
{
	int algoId;
	size_t workspaceSize;
	float wavesCount;
} cublasLtMatmulHeuristicResult_t;

#define CUBLAS_STATUS_NOT_SUPPORTED 1

/* Handle */
cublasStatus_t cublasLtCreate(cublasLtHandle_t* handle) 
{
	(void)handle;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtDestroy(cublasLtHandle_t handle) 
{
	(void)handle;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}

/* Matmul operation descriptor */
cublasStatus_t cublasLtMatmulDescCreate(cublasLtMatmulDesc_t* operationDesc,
										cublasComputeType_t computeType,
										cudaDataType scaleType) 
{
	(void)operationDesc;
	(void)computeType;
	(void)scaleType;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatmulDescDestroy(cublasLtMatmulDesc_t operationDesc) 
{
	(void)operationDesc;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatmulDescSetAttribute(
	cublasLtMatmulDesc_t operationDesc, int attr, const void* buf,
	size_t sizeInBytes) 
{
	(void)operationDesc;
	(void)attr;
	(void)buf;
	(void)sizeInBytes;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatmulDescGetAttribute(
	cublasLtMatmulDesc_t operationDesc, int attr, void* buf, size_t sizeInBytes,
	size_t* sizeWritten) 
{
	(void)operationDesc;
	(void)attr;
	(void)buf;
	(void)sizeInBytes;
	(void)sizeWritten;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}

/* Matrix layouts */
cublasStatus_t cublasLtMatrixLayoutCreate(cublasLtMatrixLayout_t* layout,
										  cudaDataType type, long long rows,
										  long long cols, long long ld) 
{
	(void)layout;
	(void)type;
	(void)rows;
	(void)cols;
	(void)ld;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatrixLayoutDestroy(cublasLtMatrixLayout_t layout) 
{
	(void)layout;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatrixLayoutSetAttribute(cublasLtMatrixLayout_t layout,
												int attr, const void* buf,
												size_t sizeInBytes) 
{
	(void)layout;
	(void)attr;
	(void)buf;
	(void)sizeInBytes;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatrixLayoutGetAttribute(cublasLtMatrixLayout_t layout,
												int attr, void* buf,
												size_t sizeInBytes,
												size_t* sizeWritten) 
{
	(void)layout;
	(void)attr;
	(void)buf;
	(void)sizeInBytes;
	(void)sizeWritten;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}

/* Preference */
cublasStatus_t cublasLtMatmulPreferenceCreate(
	cublasLtMatmulPreference_t* pref) 
{
	(void)pref;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatmulPreferenceDestroy(
	cublasLtMatmulPreference_t pref) 
{
	(void)pref;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatmulPreferenceSetAttribute(
	cublasLtMatmulPreference_t pref, int attr, const void* buf,
	size_t sizeInBytes) 
{
	(void)pref;
	(void)attr;
	(void)buf;
	(void)sizeInBytes;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}
cublasStatus_t cublasLtMatmulPreferenceGetAttribute(
	cublasLtMatmulPreference_t pref, int attr, void* buf, size_t sizeInBytes,
	size_t* sizeWritten) 
{
	(void)pref;
	(void)attr;
	(void)buf;
	(void)sizeInBytes;
	(void)sizeWritten;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}

/* Heuristics */
cublasStatus_t cublasLtMatmulAlgoGetHeuristic(
	cublasLtHandle_t handle, cublasLtMatmulDesc_t operationDesc,
	cublasLtMatrixLayout_t Adesc, cublasLtMatrixLayout_t Bdesc,
	cublasLtMatrixLayout_t Cdesc, cublasLtMatrixLayout_t Ddesc,
	const cublasLtMatmulPreference_t preference, int requestedAlgoCount,
	cublasLtMatmulHeuristicResult_t heuristicResultsArray[],
	int* returnAlgoCount) 
{
	(void)handle;
	(void)operationDesc;
	(void)Adesc;
	(void)Bdesc;
	(void)Cdesc;
	(void)Ddesc;
	(void)preference;
	(void)requestedAlgoCount;
	(void)heuristicResultsArray;
	(void)returnAlgoCount;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}

/* Matmul */
cublasStatus_t cublasLtMatmul(
	cublasLtHandle_t handle, cublasLtMatmulDesc_t operationDesc,
	const void* alpha, const void* A, cublasLtMatrixLayout_t Adesc,
	const void* B, cublasLtMatrixLayout_t Bdesc, const void* beta,
	const void* C, cublasLtMatrixLayout_t Cdesc, void* D,
	cublasLtMatrixLayout_t Ddesc, const cublasLtMatmulAlgo_t algo,
	void* workspace, size_t workspaceSizeInBytes, cudaStream_t stream) 
{
	(void)handle;
	(void)operationDesc;
	(void)alpha;
	(void)A;
	(void)Adesc;
	(void)B;
	(void)Bdesc;
	(void)beta;
	(void)C;
	(void)Cdesc;
	(void)D;
	(void)Ddesc;
	(void)algo;
	(void)workspace;
	(void)workspaceSizeInBytes;
	(void)stream;
	return CUBLAS_STATUS_NOT_SUPPORTED;
}

#ifdef __cplusplus
}
#endif
