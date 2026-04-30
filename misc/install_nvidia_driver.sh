#!/usr/bin/env bash

#
# This script installs NVIDIA driver and CUDA Toolkit on the host machine.
# 
# Support OS
# - Ubuntu 18.04/20.04/22.04/24.04
# - Rocky Linux 8/9
#
# Minimum Driver/Toolkit Versions
# - NVIDIA driver: 535
# - CUDA Toolkit: 12.0

set -euo pipefail

NVIDIA_DRIVER_VERSION="535"
CUDA_TOOLKIT_PACKAGE="12-0"

OS_ID=""
OS_NAME=""
OS_VERSION_ID=""
OS_MAJOR=""
OS_MINOR=""

# Set noninteractive mode for apt-get to avoid prompts during package installation
export DEBIAN_FRONTEND=noninteractive


log() { echo "[$(date +"%F %T")] $*"; }

run() {
    log "RUN: $*"
    "$@"
}

die() {
    echo "$*" >&2
    exit 1
}

require_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        echo "Please run as root (e.g. sudo $0 ...)"
        exit 1
    fi
}

require_cmd() {
    local cmd="$1"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "Required command not found: ${cmd}"
        exit 1
    fi
}

detect_os() {
    if [[ ! -f /etc/os-release ]]; then
        die "Unsupported OS: /etc/os-release not found"
    fi

    . /etc/os-release

    OS_ID="${ID:-}"
    OS_NAME="${NAME:-unknown}"
    OS_VERSION_ID="${VERSION_ID:-0}"
    OS_MAJOR="${OS_VERSION_ID%%.*}"
    if [[ "${OS_VERSION_ID}" == *.* ]]; then
        OS_MINOR="${OS_VERSION_ID#*.}"
    else
        OS_MINOR="0"
    fi

    log "Detected OS: ${OS_NAME} (${OS_ID} ${OS_VERSION_ID} / major: ${OS_MAJOR}, minor: ${OS_MINOR})"
}

usage() {
    cat <<EOF
Usage: $0 [options]

Options:
    -n, --nvidia_driver <version>   NVIDIA driver version (default: ${NVIDIA_DRIVER_VERSION})
    -c, --cuda_toolkit <package>    CUDA toolkit package (default: ${CUDA_TOOLKIT_PACKAGE})
    -l, --list                      List available versions for current OS
    -h, --help                      Show this help
EOF
}

need_reboot_exit() {
    echo
    echo "Nouveau driver was disabled. Reboot is required."
    echo "After reboot, re-run this script to complete installation."
    exit 2
}

has_nouveau_loaded() {
    if ! command -v lsmod >/dev/null 2>&1; then
        return 1
    fi
    lsmod | awk '{print $1}' | grep -qx "nouveau"
}

disable_nouveau_ubuntu() {
    local conf="/etc/modprobe.d/blacklist-nouveau.conf"
    if [[ ! -f "${conf}" ]] || ! grep -q "blacklist nouveau" "${conf}"; then
        cat > "${conf}" <<EOF
blacklist nouveau
blacklist lbm-nouveau
options nouveau modeset=0
alias nouveau off
alias lbm-nouveau off
EOF
    fi

    run update-initramfs -u
    need_reboot_exit
}

disable_nouveau_rhel_family() {
    local conf="/etc/modprobe.d/blacklist-nouveau.conf"
    if [[ ! -f "${conf}" ]] || ! grep -q "blacklist nouveau" "${conf}"; then
        cat > "${conf}" <<EOF
blacklist nouveau
options nouveau modeset=0
EOF
    fi

    if command -v dracut >/dev/null 2>&1; then
        run dracut --force
    fi

    need_reboot_exit
}


ubuntu_cuda_repo_dist() {
    case "${OS_MAJOR}" in
        18) echo "ubuntu1804" ;;
        20) echo "ubuntu2004" ;;
        22) echo "ubuntu2204" ;;
        24) echo "ubuntu2404" ;;
        *) return 1 ;;
    esac
}

select_equal_or_nearest_higher_version() {
    local current_version="$1"
    local nearest_version=""
    local candidate_version=""

    while IFS= read -r candidate_version; do
        [[ -n "${candidate_version}" ]] || continue

        if [[ "${candidate_version}" == "${current_version}" ]]; then
            echo "${candidate_version}"
            return 0
        fi

        if [[ "$(printf '%s\n%s\n' "${current_version}" "${candidate_version}" | sort -V | tail -n 1)" == "${candidate_version}" ]]; then
            nearest_version="${candidate_version}"
            break
        fi
    done

    if [[ -z "${nearest_version}" ]]; then
        return 1
    fi

    echo "${nearest_version}"
}

find_nearest_higher_nvidia_driver_ubuntu() {
    local current_version="$1"

    if [[ ! "${current_version}" =~ ^[0-9]+$ ]]; then
        echo "Invalid NVIDIA driver version: ${current_version}" >&2
        return 1
    fi

    require_cmd apt-cache

    apt-cache search '^nvidia-driver-[0-9]+' 2>/dev/null \
        | awk '$1 ~ /^nvidia-driver-[0-9]+$/ {sub(/^nvidia-driver-/, "", $1); print $1}' \
        | sort -Vu \
        | select_equal_or_nearest_higher_version "${current_version}"
}

find_nearest_higher_cuda_toolkit_ubuntu() {
    local current_version="$1"

    if [[ ! "${current_version}" =~ ^[0-9]+(-[0-9]+)+$ ]]; then
        echo "Invalid CUDA toolkit version: ${current_version}" >&2
        return 1
    fi

    require_cmd apt-cache

    apt-cache search '^cuda-toolkit-[0-9-]+' 2>/dev/null \
        | awk '$1 ~ /^cuda-toolkit-[0-9-]+$/ {sub(/^cuda-toolkit-/, "", $1); print $1}' \
        | sort -Vu \
        | select_equal_or_nearest_higher_version "${current_version}"
}

find_nearest_higher_nvidia_driver_rocky() {
    local current_version="$1"

    if [[ ! "${current_version}" =~ ^[0-9]+$ ]]; then
        echo "Invalid NVIDIA driver version: ${current_version}" >&2
        return 1
    fi

    require_cmd dnf

    dnf module list nvidia-driver --all 2>/dev/null \
        | awk '$1 == "nvidia-driver" && $2 ~ /^[0-9]+-dkms$/ {print $2}' \
        | sort -Vu \
        | select_equal_or_nearest_higher_version "${current_version}"
}

find_nearest_higher_cuda_toolkit_rocky() {
    local current_version="$1"

    if [[ ! "${current_version}" =~ ^[0-9]+(-[0-9]+)+$ ]]; then
        echo "Invalid CUDA toolkit version: ${current_version}" >&2
        return 1
    fi

    require_cmd dnf

    dnf list available 'cuda-toolkit*' 2>/dev/null \
        | awk '/^cuda-toolkit-[0-9-]+(\.|$)/ {
            pkg=$1
            sub(/^cuda-toolkit-/, "", pkg)
            sub(/\..*$/, "", pkg)
            if (pkg ~ /^[0-9]+(-[0-9]+)*$/) print pkg
        }' \
        | sort -Vu \
        | select_equal_or_nearest_higher_version "${current_version}"
}

remove_existing_packages_ubuntu() {
    log "Removing existing NVIDIA driver and CUDA Toolkit packages (Ubuntu ${OS_MAJOR})"
    run apt-get remove -y --purge 'nvidia-*' 'cuda-*' 'libnvidia-*' 'nsight-*' || true
    run apt-get autoremove -y || true
    run apt-get autoclean -y || true

    # Disable nouveau
    if has_nouveau_loaded; then
        log "nouveau is loaded. Disabling it first."
        disable_nouveau_ubuntu
    fi
}

remove_existing_packages_rocky() {
    log "Removing existing NVIDIA driver and CUDA Toolkit packages (Rocky ${OS_MAJOR})"
    run dnf -y remove 'nvidia-driver*' 'cuda*' 'libnvidia*' 'nsight*' || true
    run dnf module reset -y nvidia-driver || true
    run dnf clean all

    if has_nouveau_loaded; then
        log "nouveau is loaded. Disabling it first."
        disable_nouveau_rhel_family
    fi
}

register_repository_ubuntu() {
    local repo_dist="$(ubuntu_cuda_repo_dist)"
    local repo_base="https://developer.download.nvidia.com/compute/cuda/repos/${repo_dist}/x86_64"
    local pin_file="cuda-${repo_dist}.pin"
    local key_url="${repo_base}/3bf863cc.pub"

    run curl -fsSLo "${pin_file}" "${repo_base}/${pin_file}"
    run install -m 644 "${pin_file}" /etc/apt/preferences.d/cuda-repository-pin-600
    run rm -f "${pin_file}"
    run apt-key adv --fetch-keys "${key_url}"
    run add-apt-repository -y "deb ${repo_base}/ /"
    run apt-get update -y
}

register_repository_rocky() {
    local repo_base="https://developer.download.nvidia.com/compute/cuda/repos/rhel${OS_MAJOR}/x86_64"
    local repo_file="/etc/yum.repos.d/cuda-rhel${OS_MAJOR}.repo"

    if [[ ! -f "${repo_file}" ]]; then
        run dnf config-manager --add-repo="${repo_base}/cuda-rhel${OS_MAJOR}.repo"
        run dnf clean all
    fi
}

resolve_target_versions_ubuntu() {
    local target_nvidia_version
    local target_cuda_toolkit_package

    target_nvidia_version="$(find_nearest_higher_nvidia_driver_ubuntu "${NVIDIA_DRIVER_VERSION}")" || \
        die "Requested nvidia-driver-${NVIDIA_DRIVER_VERSION} is unavailable and no equal/higher version was found."

    target_cuda_toolkit_package="$(find_nearest_higher_cuda_toolkit_ubuntu "${CUDA_TOOLKIT_PACKAGE}")" || \
        die "Requested cuda-toolkit-${CUDA_TOOLKIT_PACKAGE} is unavailable and no equal/higher version was found."

    echo "${target_nvidia_version};${target_cuda_toolkit_package}"
}

resolve_target_versions_rocky() {
    local target_nvidia_version
    local target_cuda_toolkit_package

    target_nvidia_version="$(find_nearest_higher_nvidia_driver_rocky "${NVIDIA_DRIVER_VERSION}")" || \
        die "Requested nvidia-driver-${NVIDIA_DRIVER_VERSION} is unavailable and no equal/higher version was found."

    target_cuda_toolkit_package="$(find_nearest_higher_cuda_toolkit_rocky "${CUDA_TOOLKIT_PACKAGE}")" || \
        die "Requested cuda-toolkit-${CUDA_TOOLKIT_PACKAGE} is unavailable and no equal/higher version was found."

    echo "${target_nvidia_version};${target_cuda_toolkit_package}"
}

install_ubuntu() {
    # Check if the OS version is supported
    if [[ "${OS_MAJOR}" != "18" && "${OS_MAJOR}" != "20" && "${OS_MAJOR}" != "22" && "${OS_MAJOR}" != "24" ]]; then
        die "Unsupported Ubuntu version: ${OS_VERSION_ID}"
    fi

    require_cmd apt-cache
    run apt-get install -y --no-install-recommends \
        apt-utils \
        ca-certificates \
        curl \
        gnupg2 \
        lshw \
        software-properties-common \
        ubuntu-drivers-common

    # Uninstalling a previously installed NVIDIA Driver
    remove_existing_packages_ubuntu

    # Registering the NVIDIA repository
    register_repository_ubuntu

    local selected_versions
    selected_versions="$(resolve_target_versions_ubuntu)"
    local target_nvidia_version="${selected_versions%%;*}"
    local target_cuda_toolkit_package="${selected_versions##*;}"

    log "Selected NVIDIA driver version: ${target_nvidia_version}"
    log "Selected CUDA toolkit package: ${target_cuda_toolkit_package}"

    # Installing the NVIDIA driver and CUDA toolkit packages
    run apt-get install -y --no-install-recommends "nvidia-driver-${target_nvidia_version}"
    run apt-get install -y --no-install-recommends "cuda-toolkit-${target_cuda_toolkit_package}"
}

install_rocky() {
    # Check if the OS version is supported
    if [[ "${OS_MAJOR}" != "8" && "${OS_MAJOR}" != "9" ]]; then
        die "Unsupported Rocky version: ${OS_VERSION_ID}"
    fi

    require_cmd dnf
    run dnf update -y
    run dnf install -y epel-release dnf-plugins-core lshw

    # Uninstalling a previously installed NVIDIA Driver
    remove_existing_packages_rocky

    # Registering the NVIDIA repository
    register_repository_rocky

    local selected_versions
    selected_versions="$(resolve_target_versions_rocky)"
    local target_nvidia_version="${selected_versions%%;*}"
    local target_cuda_toolkit_package="${selected_versions##*;}"

    log "Selected NVIDIA driver version: ${target_nvidia_version}"
    log "Selected CUDA toolkit package: ${target_cuda_toolkit_package}"

    # Installing the NVIDIA driver and CUDA toolkit packages
    run dnf module install -y "nvidia-driver:${target_nvidia_version}"
    run dnf install -y "cuda-toolkit-${target_cuda_toolkit_package}"
}

list_available_versions() {
    case "${OS_ID}" in
        ubuntu)
            require_cmd apt-cache
            register_repository_ubuntu

            echo "[Ubuntu ${OS_MAJOR}] Available NVIDIA Driver(SDK) packages"
            apt-cache search '^nvidia-driver-[0-9]+' | awk '{print $1}' | sort -Vu || true
            echo "[Ubuntu ${OS_MAJOR}] Available CUDA Toolkit packages"
            apt-cache search '^cuda-toolkit-[0-9-]+' | awk '{print $1}' | sort -Vu || true            
            ;;
        rocky)
            require_cmd dnf
            register_repository_rocky

            echo "[Rocky ${OS_MAJOR}] Available NVIDIA Driver(SDK) module streams"
            dnf module list nvidia-driver --all 2>/dev/null | awk 'NR==1 || /^nvidia-driver/ {print}' || true
            echo "[Rocky ${OS_MAJOR}] Available CUDA Toolkit packages"
            dnf list available 'cuda-toolkit*' 2>/dev/null | awk '/^cuda-toolkit/ {print $1}' | sort -Vu || true
            ;;
        *)
            die "Listing is not supported on ${OS_NAME} (${OS_ID} ${OS_VERSION_ID})."
            ;;
    esac
}

parse_args() {
    while (($#)); do
        case "$1" in
            -n|--nvidia_driver)
                [[ $# -ge 2 ]] || { echo "Missing value for $1"; exit 1; }
                NVIDIA_DRIVER_VERSION="$2"
                shift 2
                ;;
            -c|--cuda_toolkit)
                [[ $# -ge 2 ]] || { echo "Missing value for $1"; exit 1; }
                CUDA_TOOLKIT_PACKAGE="$2"
                shift 2
                ;;
            -l|--list)
                list_available_versions
                exit 0
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                echo "Unknown option: $1" >&2
                usage
                exit 1
                ;;
        esac
    done
}

main() {
    echo "##########################################################################################"
    echo " Install NVIDIA drivers and CUDA Toolkit"
    echo "##########################################################################################"
    
    detect_os
    parse_args "$@"
    require_root

    log "NVIDIA Driver version: ${NVIDIA_DRIVER_VERSION}"
    log "CUDA Toolkit package: ${CUDA_TOOLKIT_PACKAGE}"

    case "${OS_ID}" in
        ubuntu)
            install_ubuntu
            ;;
        rocky)
            install_rocky
            ;;
        *)
            die "This script does not support ${OS_NAME} (${OS_ID} ${OS_VERSION_ID})."
            ;;
    esac
}

main "$@"


