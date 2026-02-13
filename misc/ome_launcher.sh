#!/bin/bash

PREFIX=/opt/ovenmediaengine

##########################################################################################
# Environment Variables
##########################################################################################
if [[ "$OSTYPE" == "darwin"* ]]; then
    NCPU=$(sysctl -n hw.ncpu)
    OSNAME=$(sw_vers -productName)
    OSVERSION=$(sw_vers -productVersion)
else
    NCPU=$(nproc)

    # CentOS, Fedora
    if [ -f /etc/redhat-release ]; then
        OSNAME=$(cat /etc/redhat-release |awk '{print $1}')
        OSVERSION=$(cat /etc/redhat-release |sed s/.*release\ // |sed s/\ .*// | cut -d"." -f1)
    # Ubuntu
    elif [ -f /etc/os-release ]; then
        OSNAME=$(cat /etc/os-release | grep "^NAME" | tr -d "\"" | cut -d"=" -f2)
        OSVERSION=$(cat /etc/os-release | grep ^VERSION= | tr -d "\"" | cut -d"=" -f2 | cut -d"." -f1 | awk '{print  $1}')
        OSMINORVERSION=$(cat /etc/os-release | grep ^VERSION= | tr -d "\"" | cut -d"=" -f2 | cut -d"." -f2 | awk '{print  $1}')
    fi
fi

##########################################################################################
# Preload the installed drivers
##########################################################################################
check_xilinx_driver() {
    if [[ -f /opt/xilinx/xcdr/xrmd_start.bash ]]; then
        LIBXRM_PATH=/opt/xilinx/xrm/lib/libxrm.so.1
        LIBXRT_CORE_PATH=/opt/xilinx/xrt/lib/libxrt_core.so.2
        LIBXRT_COREUTIL_PATH=/opt/xilinx/xrt/lib/libxrt_coreutil.so.2
        LIBXMA2API_PATH=/opt/xilinx/xrt/lib/libxma2api.so.2

        # source /opt/xilinx/xcdr/setup.sh -f
        source /opt/xilinx/xrt/setup.sh > /dev/null 2>&1
        source /opt/xilinx/xrm/setup.sh > /dev/null 2>&1
        source /opt/xilinx/xcdr/xrmd_start.bash

        if [ -f $LIBXRM_PATH ] && [ -f $LIBXRT_CORE_PATH ] && [ -f $LIBXRT_COREUTIL_PATH ] && [ -f $LIBXMA2API_PATH ]; then
            export LD_PRELOAD=$LIBXRM_PATH:$LIBXRT_CORE_PATH:$LIBXRT_COREUTIL_PATH:$LIBXMA2API_PATH:$LD_PRELOAD
        fi
    fi
}

check_nvidia_driver() {
    LIB_PATH=""
    LIB_FILES=("libnvidia-ml.so.1" "libcuda.so.1" "libnppicc.so.10" "libnppig.so.10" "libcudart.so.10.0" "libcublas.so.10" "libnppicc.so.11" "libnppig.so.11" "libcudart.so.11.0" "libcublas.so.11" "libnppicc.so.12" "libnppig.so.12" "libcudart.so.12.0" "libcublas.so.12")

    if [ "${OSNAME}" == "Ubuntu" ]; then
        LIB_PATH=/lib/x86_64-linux-gnu
    elif  [ "${OSNAME}" == "CentOS" ] || [ "${OSNAME}" == "Amazon Linux" ] || [ "${OSNAME}" == "Rocky" ]; then
        LIB_PATH=/usr/lib64
    else
        return
    fi

    for LIB_FILE in ${LIB_FILES[@]}; do
        if [ -f ${LIB_PATH}/${LIB_FILE} ]; then
            export LD_PRELOAD=${LIB_PATH}/${LIB_FILE}:${LD_PRELOAD}
        fi
    done
}

# Clear the LD_PRELOAD
export LD_PRELOAD=""

# Check the installed drivers and preload them.
check_xilinx_driver
check_nvidia_driver

export LD_LIBRARY_PATH=${PREFIX}/lib:${PREFIX}/lib64:${PREFIX}/lib/stubs

echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo "LD_PRELOAD: $LD_PRELOAD"

# Run as daemon 
exec /usr/bin/OvenMediaEngine "$@"