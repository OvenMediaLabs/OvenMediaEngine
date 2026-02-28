#!/bin/bash
#==============================================================================
#
#  OvenMediaEngine Launcher
#
#  Created by Keukhan
#  Copyright (c) 2023 OvenMediaLabs. All rights reserved.
#
#==============================================================================

set -euo pipefail

VERSION="v0.1"
DEFAULT_LIB_DIR="${OME_LIB_DIR:-/opt/ovenmediaengine}"
DEFAULT_APP_DIR="${OME_APP_DIR:-/usr/share/ovenmediaengine}"
DEFAULT_EXE_BIN="${OME_EXE_BIN:-OvenMediaEngine}"


#############################################################################################
# Misc utility functions
#############################################################################################
prepare_colors()
{
	local COLORS=$(tput colors 2>/dev/null)

	if [[ -n "${COLORS}" && ${COLORS} -ge 8 ]]
	then
		COLOR_BOLD="$(tput bold)"
		COLOR_UNDERLINE="$(tput smul)"
		COLOR_STANDOUT="$(tput smso)"
		COLOR_RESET="$(tput sgr0)"

		COLOR_BLACK="$(tput setaf 0)"
		COLOR_RED="$(tput setaf 1)"
		COLOR_GREEN="$(tput setaf 2)"
		COLOR_YELLOW="$(tput setaf 3)"
		COLOR_BLUE="$(tput setaf 4)"
		COLOR_MAGENTA="$(tput setaf 5)"
		COLOR_CYAN="$(tput setaf 6)"
		COLOR_WHITE="$(tput setaf 7)"
		COLOR_GRAY="$(tput setaf 8)"
	fi

	PRESET_HIGHLIGHT="${COLOR_CYAN}"
}

logd()
{
	if ! ${DEBUG}
	then
		return
	fi

	local ECHO_OPTIONS=()

	while [ $# -gt 0 ]
	do
		local ARG="$1"
		[[ "${ARG}" == -* ]] || break
		
		ECHO_OPTIONS+=("${ARG}")
		shift
	done

	local ARGS=("$@")
	echo "${ECHO_OPTIONS[@]}" "${COLOR_CYAN}${ARGS[@]}${COLOR_RESET}"
}

logi()
{
	local ECHO_OPTIONS=()

	while [ $# -gt 0 ]
	do
		local ARG="$1"
		[[ "${ARG}" == -* ]] || break
		
		ECHO_OPTIONS+=("${ARG}")
		shift
	done

	local ARGS=("$@")
	echo "${ECHO_OPTIONS[@]}" "${ARGS[@]}${COLOR_RESET}"
}

logw()
{
	local ECHO_OPTIONS=()

	while [ $# -gt 0 ]
	do
		local ARG="$1"
		[[ "${ARG}" == -* ]] || break
		
		ECHO_OPTIONS+=("${ARG}")
		shift
	done

	local ARGS=("$@")
	echo "${ECHO_OPTIONS[@]}" "${COLOR_YELLOW}${ARGS[@]}${COLOR_RESET}"
}

loge()
{
	local ECHO_OPTIONS=()

	while [ $# -gt 0 ]
	do
		local ARG="$1"
		[[ "${ARG}" == -* ]] || break
		
		ECHO_OPTIONS+=("${ARG}")
		shift
	done

	local ARGS=("$@")
	echo "${ECHO_OPTIONS[@]}" "${COLOR_RED}${ARGS[@]}${COLOR_RESET}"
}

die()
{
	local ARGS=("$@")
	loge "${ARGS[@]}"
	exit 1
}

banner()
{
	${HIDE_BANNER} && return

	logi
	logi "${COLOR_YELLOW} ▄██████▀███▄ "
	logi "${COLOR_YELLOW}█████▀ ▄██████${COLOR_RESET}  ${COLOR_YELLOW}OvenMediaEngine${COLOR_RESET} Launcher ${VERSION}"
	logi "${COLOR_YELLOW}███▄▄▄▄▀▀▀▀███"
	logi "${COLOR_YELLOW}██████▀ ▄█████${COLOR_RESET}  ${COLOR_BLUE}https://github.com/OvenMediaLabs/OvenMediaEngine"
	logi "${COLOR_YELLOW} ▀███▄██████▀ "
	logi
}

#############################################################################################
# LD_PRELOAD or LD_LIBRARY_PATH manipulation functions
#############################################################################################
append_ld_preload() {
    local lib_path="$1"
    logd "Checking library for PRELOAD_PATH ${lib_path}"
    [[ -f "${lib_path}" ]] || return 0

    if [[ -z "${LD_PRELOAD:-}" ]]; then
        LD_PRELOAD="${lib_path}"
    elif [[ ":${LD_PRELOAD}:" != *":${lib_path}:"* ]]; then
        LD_PRELOAD="${lib_path}:${LD_PRELOAD}"
    fi
}

append_ld_library_path() {
    local lib_path="$1"
    logd "Checking library path for LD_LIBRARY_PATH: ${lib_path}"
    [[ -d "${lib_path}" ]] || return 0

    if [[ -z "${LD_LIBRARY_PATH:-}" ]]; then
        LD_LIBRARY_PATH="${lib_path}"
    elif [[ ":${LD_LIBRARY_PATH}:" != *":${lib_path}:"* ]]; then
        LD_LIBRARY_PATH="${lib_path}:${LD_LIBRARY_PATH}"
    fi
}

find_lib() {
    local soname="$1"

    # First, try to find the library using ldconfig
    if command -v ldconfig >/dev/null 2>&1; then
        ldconfig -p 2>/dev/null | awk -v s="$soname" '$1==s {print $NF; exit}'
        return 0
    fi

    # If ldconfig is not available, search common library directories
    find /lib /lib64 /usr/lib /usr/lib64 /usr/local/lib -name "${soname}" -type f 2>/dev/null | head -n 1
}

add_preload_if_exists() {
    local soname="$1"
    local path
    path="$(find_lib "$soname")"
    if [[ -n "$path" && -f "$path" ]]; then
        append_ld_preload "$path"
    fi
}

load_library_set_if_complete() {
    local label="$1"
    shift

    local resolved_paths=()
    local lib_file
    for lib_file in "$@"; do
        local lib_path=""
        lib_path="$(find_lib "${lib_file}")"
        if [[ -z "${lib_path}" || ! -f "${lib_path}" ]]; then
            logw "${label} libraries are not loaded. Missing: ${lib_file}"
            return 1
        fi

        resolved_paths+=("${lib_path}")
    done

    for lib_path in "${resolved_paths[@]}"; do
        append_ld_preload "${lib_path}"
    done

    logi "${label} libraries loaded: ${resolved_paths[*]}"
    return 0
}

show_stub_linkage() {
    local bin_name="$1"

    if ! command -v ldd >/dev/null 2>&1; then
        logw "ldd command not found. Skipping stub linkage check."
        return 0
    fi

    local stub_lines
    stub_lines="$(ldd "${bin_name}" | grep stubs || true)"

    if [[ -n "${stub_lines}" ]]; then
        logw "Stub-linked libraries:"
        logw "${stub_lines}"
    else
        logd "No stub-linked libraries found in ${bin_name}"
    fi
}


##########################################################################################
# Preload the installed drivers
##########################################################################################
preload_xilinx_driver() {
    logi "Checking for XILINX/XMA drivers to preload..."

    if [[ -f /opt/xilinx/xcdr/xrmd_start.bash ]]; then
        local lib_files=(
            "/opt/xilinx/xrm/lib/libxrm.so.1"
            "/opt/xilinx/xrt/lib/libxrt_core.so.2"
            "/opt/xilinx/xrt/lib/libxrt_coreutil.so.2"
            "/opt/xilinx/xrt/lib/libxma2api.so.2"
        )

        if ! load_library_set_if_complete "XILINX/XMA" "${lib_files[@]}"; then
            return
        fi

        # source /opt/xilinx/xcdr/setup.sh -f
        source /opt/xilinx/xrt/setup.sh > /dev/null 2>&1 || true
        source /opt/xilinx/xrm/setup.sh > /dev/null 2>&1 || true
        source /opt/xilinx/xcdr/xrmd_start.bash || true
    else 
        logw "XILINX/XMA drivers not found, skipping preload."
    fi
}

preload_nvidia_driver() {
    logi "Checking for NVIDIA/CUDA drivers to preload..."

    # CUDA 10.x libraries. It will be deprecated soon. 
    local lib_files_10=(
        "libnvidia-ml.so.1"
        "libcuda.so.1"
        "libnppicc.so.10" 
        "libnppig.so.10" 
        "libcudart.so.10.0" 
        "libcublas.so.10"
        "libcublasLt.so.10"
    )

    # CUDA 11.x libraries. It will be deprecated soon.
    local lib_files_11=(
        "libnvidia-ml.so.1"
        "libcuda.so.1"
        "libnppicc.so.11" 
        "libnppig.so.11" 
        "libcudart.so.11.0" 
        "libcublas.so.11"
        "libcublasLt.so.11"
    )

    # CUDA 12.x libraries
    local lib_files_12=(
        "libnvidia-ml.so.1"
        "libcuda.so.1"
        "libnppicc.so.12" 
        "libnppig.so.12" 
        "libcudart.so.12.0"
        "libcublas.so.12"
        "libcublasLt.so.12"
    )

    if load_library_set_if_complete "NVIDIA/CUDA 12.x" "${lib_files_12[@]}"; then
        return
    fi

    if load_library_set_if_complete "NVIDIA/CUDA 11.x" "${lib_files_11[@]}"; then
        return
    fi

    if load_library_set_if_complete "NVIDIA/CUDA 10.x" "${lib_files_10[@]}"; then
        return
    fi

    logd "No complete NVIDIA/CUDA library set found. Skipping NVIDIA preload."
}

############################################################################################
# Main execution starts here
############################################################################################

resolve_ome_binary() {
    local script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    local local_ome_bin="${script_dir}/${DEFAULT_EXE_BIN}"
    local fallback_ome_bin="${DEFAULT_APP_DIR}/${DEFAULT_EXE_BIN}"

    if [[ -x "${local_ome_bin}" ]]; then
        echo "${local_ome_bin}"
        return 0
    fi

    if [[ -x "${fallback_ome_bin}" ]]; then
        echo "${fallback_ome_bin}"
        return 0
    fi

    return 1
}

DEBUG=true
HIDE_BANNER=false
prepare_colors
banner

# Check the installed drivers and preload them.
LD_PRELOAD=""
preload_xilinx_driver
preload_nvidia_driver

# Set LD_LIBRARY_PATH (prepend required paths while preserving the existing value).
append_ld_library_path "${DEFAULT_LIB_DIR}/lib/stubs"
append_ld_library_path "${DEFAULT_LIB_DIR}/lib64"
append_ld_library_path "${DEFAULT_LIB_DIR}/lib"

export LD_LIBRARY_PATH
export LD_PRELOAD

logd "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH}"
logd "LD_PRELOAD: ${LD_PRELOAD}"

if OME_BIN="$(resolve_ome_binary)"; then
    OME_BIN_DIR="$(cd -- "$(dirname -- "${OME_BIN}")" && pwd)"
    OME_BIN_NAME="$(basename -- "${OME_BIN}")"

    cd -- "${OME_BIN_DIR}"
    show_stub_linkage "${OME_BIN_NAME}"

    logi "Working directory: ${OME_BIN_DIR}"
    logi "Trying to execute: ${OME_BIN_NAME}"
 
    exec "./${OME_BIN_NAME}" "$@"
else
    loge "OvenMediaEngine executable not found."
fi    

exit 1