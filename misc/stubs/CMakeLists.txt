# ==============================================================================
# GPU / Accelerator Stub Shared Libraries
#
# These are thin stub .so files that provide the required symbols for NVIDIA
# (CUDA/cuBLAS/nvidia-ml) and Xilinx (XMA/XRM/XRT) drivers so that
# OvenMediaEngine can be run on machines that do not have the vendor drivers
# installed at build time — the real driver .so is loaded at runtime via
# LD_LIBRARY_PATH / ldconfig.
#
# Each stub is built with:
#   gcc -shared -fPIC -Wl,-soname,<soname> -Wl,--version-script=<map> -o <out> <src.c>
#
# Output is installed to ${CMAKE_INSTALL_PREFIX}/lib/stubs.
# ==============================================================================

set(STUB_OUTPUTS "")

# ------------------------------------------------------------------------------
# Helper: compile one stub shared library via add_custom_command so we get
# exact control over the output filename, soname, and version script.
# ------------------------------------------------------------------------------
function(add_stub_so output_so source_c soname map_file)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/${output_so}")
    add_custom_command(
        OUTPUT  "${_out}"
        COMMAND ${CMAKE_C_COMPILER}
                    -shared -fPIC -Wall
                    -Wl,-soname,${soname}
                    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/${map_file}
                    -o "${_out}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/${source_c}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source_c}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${map_file}"
        COMMENT "Stub: ${output_so}"
        VERBATIM
    )
    list(APPEND STUB_OUTPUTS "${_out}")
    set(STUB_OUTPUTS "${STUB_OUTPUTS}" PARENT_SCOPE)
endfunction()

# ==============================================================================
# NVIDIA stubs
# ==============================================================================
add_stub_so(libnvidia-ml.so.1   libnvidia-ml_stub.c   libnvidia-ml.so.1   libnvidia-ml.map)
add_stub_so(libcuda.so.1        libcuda_stub.c         libcuda.so.1        libcuda.map)
# cudart v11 / v12  (same source, different version scripts + sonames)
add_stub_so(libcudart.so.11.0   libcudart11_stub.c     libcudart.so.11.0   libcudart11.map)
add_stub_so(libcudart.so.12     libcudart12_stub.c     libcudart.so.12     libcudart12.map)
# cublas v11 / v12
add_stub_so(libcublas.so.11     libcublas_stub.c       libcublas.so.11     libcublas11.map)
add_stub_so(libcublas.so.12     libcublas_stub.c       libcublas.so.12     libcublas12.map)
# cublasLt v11 / v12
add_stub_so(libcublasLt.so.11   libcublasLt_stub.c     libcublasLt.so.11   libcublasLt11.map)
add_stub_so(libcublasLt.so.12   libcublasLt_stub.c     libcublasLt.so.12   libcublasLt12.map)

# ==============================================================================
# Xilinx stubs
# ==============================================================================
add_stub_so(libxma2api.so.2     libxma2api_stub.c      libxma2api.so.2     libxma2api.map)
add_stub_so(libxrm.so.1         libxrm_stub.c          libxrm.so.1         libxrm.map)
add_stub_so(libxrt_core.so.2    libxrt_core_stub.c     libxrt_core.so.2    libxrt_core.map)

# ==============================================================================
# Aggregate target + install
# ==============================================================================
add_custom_target(stubs ALL DEPENDS ${STUB_OUTPUTS})

install(FILES ${STUB_OUTPUTS}
    DESTINATION lib/stubs
    COMPONENT stubs
)
