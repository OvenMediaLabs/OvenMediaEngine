#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

	typedef int cublasStatus_t;
	typedef void *cublasHandle_t;
	typedef void *cudaStream_t;
	typedef int cublasMath_t;
	typedef int cublasOperation_t;
	typedef int cudaDataType;
	typedef int cublasComputeType_t;
	typedef int cublasGemmAlgo_t;

#define CUBLAS_STATUS_NOT_SUPPORTED 1

	cublasStatus_t cublasCreate_v2(cublasHandle_t *handle)
	{
		(void)handle;
		return CUBLAS_STATUS_NOT_SUPPORTED;
	}
	cublasStatus_t cublasDestroy_v2(cublasHandle_t handle)
	{
		(void)handle;
		return CUBLAS_STATUS_NOT_SUPPORTED;
	}
	cublasStatus_t cublasSetStream_v2(cublasHandle_t handle, cudaStream_t stream)
	{
		(void)handle;
		(void)stream;
		return CUBLAS_STATUS_NOT_SUPPORTED;
	}
	cublasStatus_t cublasSetMathMode(cublasHandle_t handle, cublasMath_t mode)
	{
		(void)handle;
		(void)mode;
		return CUBLAS_STATUS_NOT_SUPPORTED;
	}

	cublasStatus_t cublasSgemm_v2(
		cublasHandle_t handle, cublasOperation_t transa, cublasOperation_t transb,
		int m, int n, int k,
		const float *alpha,
		const float *A, int lda,
		const float *B, int ldb,
		const float *beta,
		float *C, int ldc)
	{
		(void)handle;
		(void)transa;
		(void)transb;
		(void)m;
		(void)n;
		(void)k;
		(void)alpha;
		(void)A;
		(void)lda;
		(void)B;
		(void)ldb;
		(void)beta;
		(void)C;
		(void)ldc;
		return CUBLAS_STATUS_NOT_SUPPORTED;
	}

	cublasStatus_t cublasGemmEx(
		cublasHandle_t handle,
		cublasOperation_t transa, cublasOperation_t transb,
		int m, int n, int k,
		const void *alpha,
		const void *A, cudaDataType Atype, int lda,
		const void *B, cudaDataType Btype, int ldb,
		const void *beta,
		void *C, cudaDataType Ctype, int ldc,
		cublasComputeType_t computeType, cublasGemmAlgo_t algo)
	{
		(void)handle;
		(void)transa;
		(void)transb;
		(void)m;
		(void)n;
		(void)k;
		(void)alpha;
		(void)A;
		(void)Atype;
		(void)lda;
		(void)B;
		(void)Btype;
		(void)ldb;
		(void)beta;
		(void)C;
		(void)Ctype;
		(void)ldc;
		(void)computeType;
		(void)algo;
		return CUBLAS_STATUS_NOT_SUPPORTED;
	}

	cublasStatus_t cublasGemmStridedBatchedEx(
		cublasHandle_t handle,
		cublasOperation_t transa, cublasOperation_t transb,
		int m, int n, int k,
		const void *alpha,
		const void *A, cudaDataType Atype, int lda, long long strideA,
		const void *B, cudaDataType Btype, int ldb, long long strideB,
		const void *beta,
		void *C, cudaDataType Ctype, int ldc, long long strideC,
		int batchCount,
		cublasComputeType_t computeType, cublasGemmAlgo_t algo)
	{
		(void)handle;
		(void)transa;
		(void)transb;
		(void)m;
		(void)n;
		(void)k;
		(void)alpha;
		(void)A;
		(void)Atype;
		(void)lda;
		(void)strideA;
		(void)B;
		(void)Btype;
		(void)ldb;
		(void)strideB;
		(void)beta;
		(void)C;
		(void)Ctype;
		(void)ldc;
		(void)strideC;
		(void)batchCount;
		(void)computeType;
		(void)algo;
		return CUBLAS_STATUS_NOT_SUPPORTED;
	}

	cublasStatus_t cublasGemmBatchedEx(
		cublasHandle_t handle,
		cublasOperation_t transa, cublasOperation_t transb,
		int m, int n, int k,
		const void *alpha,
		const void *const Aarray[], cudaDataType Atype, int lda,
		const void *const Barray[], cudaDataType Btype, int ldb,
		const void *beta,
		void *const Carray[], cudaDataType Ctype, int ldc,
		int batchCount,
		cublasComputeType_t computeType, cublasGemmAlgo_t algo)
	{
		(void)handle;
		(void)transa;
		(void)transb;
		(void)m;
		(void)n;
		(void)k;
		(void)alpha;
		(void)Aarray;
		(void)Atype;
		(void)lda;
		(void)Barray;
		(void)Btype;
		(void)ldb;
		(void)beta;
		(void)Carray;
		(void)Ctype;
		(void)ldc;
		(void)batchCount;
		(void)computeType;
		(void)algo;
		return CUBLAS_STATUS_NOT_SUPPORTED;
	}

#ifdef __cplusplus
}
#endif
