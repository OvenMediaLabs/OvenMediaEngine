#!/bin/bash

# ** This script is only available for Ubuntu and Amazon Linux OS
#
# U30 supports 
#   Amazon Linux 2 kernel 5.10, Amazon Linux 2 kernel 4.14
#   Red Hat 7.8 kernel 4.9.184
#   Ubuntu 22.04 kernel 5.15, Ubuntu 20.04 kernel 5.13, Ubuntu 20.04 kernel 5.4, Ubuntu 20.04 kernel 5.11, Ubuntu 18.04 kernel 5.4
#

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
    # Ubuntu, Amazon
    elif [ -f /etc/os-release ]; then
        OSNAME=$(cat /etc/os-release | grep "^NAME" | tr -d "\"" | cut -d"=" -f2)
        OSVERSION=$(cat /etc/os-release | grep ^VERSION= | tr -d "\"" | cut -d"=" -f2 | cut -d"." -f1 | awk '{print  $1}')
        OSMINORVERSION=$(cat /etc/os-release | grep ^VERSION= | tr -d "\"" | cut -d"=" -f2 | cut -d"." -f2 | awk '{print  $1}')
    fi
fi

CURRENT=$(pwd)
TEMP_PATH=/tmp
XMA_VERSION=3.0

echo "##########################################################################################"
echo " Install Xilinx Video SDK ${XMA_VERSION} "
echo "##########################################################################################"
echo ${OSTYPE} / ${OSNAME} / ${OSVERSION}.${OSMINORVERSION}
echo ${CURRENT}

install_xma_props_to_json_xrm()
{
    # https://github.com/Xilinx/app-xma-props-to-json-xrm/tree/U30_GA_3
    sudo apt -y install curl

    (DIR=${TEMP_PATH}/xmaPropsTojson && \
    mkdir -p ${DIR} && \
    cd ${DIR} && \
    curl -sSLf https://github.com/Xilinx/app-xma-props-to-json-xrm/archive/refs/tags/U30_GA_3.tar.gz | tar -xz --strip-components=1 && \
    mkdir -p Release && \
    cd Release && \
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3" -DCMAKE_INSTALL_PREFIX=/opt/xilinx/xrm .. && \
    make && \
    sudo make install && \
    rm -rf ${DIR} ) || fail_exit "xmaPropsTojson"
}

install_videosdk_ubuntu()
{
    # Adding Xilinx public key
    sudo apt-get -y install gnupg2
    sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys FD3DD2C355D2B701

    # Fixed xilinx driver installation not working properly on Ubuntu 20.04.
    if [ "${OSVERSION}" == "20" ]; then
        sudo apt -y install software-properties-common
        # sudo add-apt-repository -y ppa:gpxbv/apt-urlfix
        sudo apt -y autoremove
        sudo apt -y install apt apt-utils
    fi

    # Added resositroty
    # https://xilinx.github.io/video-sdk/v3.0/package_feed.html
    CODE_NAME=$(lsb_release -c -s)
    # Official repository
    echo "deb [trusted=yes] https://packages.xilinx.com/artifactory/debian-packages ${CODE_NAME} main" | sudo tee /etc/apt/sources.list.d/xilinx.list
    # Mirror repository
    # echo "deb [trusted=yes] http://xilinx-repo-mirror.ovenmediaengine.com/artifactory/debian-packages ${CODE_NAME} main" | sudo tee /etc/apt/sources.list.d/xilinx.list


    # Remove older versions of the Xilinx Video SDK
    sudo apt-get -y remove xvbm xilinx-u30-xvbm xrmu30decoder xrmu30scaler xrmu30encoder xmpsoccodecs xmultiscaler xlookahead xmaapps xmapropstojson xffmpeg launcher jobslotreservation xcdr
    sudo apt-get -y remove xrm xilinx-container-runtime xilinx-xvbm xilinx-u30-xrm-decoder xilinx-u30-xrm-encoder xilinx-u30-xrm-multiscaler xilinx-u30-xma-multiscaler xilinx-u30-xlookahead xilinx-u30-xmpsoccodecs xilinx-u30-xma-apps xilinx-u30-xmapropstojson xilinx-u30-xffmpeg xilinx-u30-launcher xilinx-u30-jobslotreservation xilinx-u30-xcdr xilinx-u30-gstreamer-1.16.2 xilinx-u30-vvas xilinx-sc-fw-u30 xilinx-u30-gen3x4-base xilinx-u30-gen3x4-validate

    # Install Required packages
    sudo apt-get -y update
    sudo apt-get -y install cmake pkg-config
    sudo apt-get -y install xrt=2.11.722
    sudo apt-mark hold xrt
    sudo apt-get -y install xilinx-alveo-u30-core

    sudo cp /opt/xilinx/xcdr/xclbins/transcode.xclbin /opt/xilinx/xcdr/xclbins/transcode_lite.xclbin
    sudo cp /opt/xilinx/xcdr/xclbins/on_prem/transcode.xclbin /opt/xilinx/xcdr/xclbins/transcode.xclbin
}

install_videosdk_amazonlinux()
{
    # Added resositroty
    # https://xilinx.github.io/video-sdk/v3.0/package_feed.html
    sudo cp xilinx.repo /etc/yum.repos.d/

    # Install Required packages
    sudo amazon-linux-extras install epel -y

    # Remove older versions of the Xilinx Video SDK
    sudo yum -y remove xvbm xilinx-u30-xvbm xrmu30decoder xrmu30scaler xrmu30encoder xmpsoccodecs xmultiscaler xlookahead xmaapps xmapropstojson xffmpeg launcher jobslotreservation xcdr
    sudo yum -y remove xrm xilinx-container-runtime xilinx-xvbm xilinx-u30-xrm-decoder xilinx-u30-xrm-encoder xilinx-u30-xrm-multiscaler xilinx-u30-xma-multiscaler xilinx-u30-xlookahead xilinx-u30-xmpsoccodecs xilinx-u30-xma-apps xilinx-u30-xmapropstojson xilinx-u30-xffmpeg xilinx-u30-launcher xilinx-u30-jobslotreservation xilinx-u30-xcdr xilinx-u30-gstreamer-1.16.2 xilinx-u30-vvas xilinx-sc-fw-u30 xilinx-u30-gen3x4-base xilinx-u30-gen3x4-validate

    sudo yum -y update
    sudo yum -y install cmake boost-devel gcc-g++
    sudo yum -y install yum-plugin-versionlock
    sudo yum -y install xrt-2.11.722-1.x86_64
    sudo yum -y versionlock xrt-2.11.722
    sudo yum -y install xilinx-alveo-u30-core

    sudo cp /opt/xilinx/xcdr/xclbins/transcode.xclbin /opt/xilinx/xcdr/xclbins/transcode_lite.xclbin
    sudo cp /opt/xilinx/xcdr/xclbins/on_prem/transcode.xclbin /opt/xilinx/xcdr/xclbins/transcode.xclbin
}

fail_exit()
{
    echo "($1) installation has failed."
    cd ${CURRENT}
    exit 1
}

check_version()
{
    if [[ "${OSNAME}" == "Ubuntu" && "${OSVERSION}" != "18" && "${OSVERSION}" != "20"  && "${OSVERSION}" != "22" ]]; then
        proceed_yn
    fi

    if [[ "${OSNAME}" == "Amazon Linux" && "${OSVERSION}" != "2" ]]; then
        proceed_yn
    fi
}

proceed_yn()
{
    read -p "This program [$0] is tested on [Ubuntu 18/20.04, Amazon Linux 2] Do you want to continue [y/N] ? " ANS
    if [[ "${ANS}" != "y" && "$ANS" != "yes" ]]; then
        cd ${CURRENT}
        exit 1
    fi
}

if [ "${OSNAME}" == "Ubuntu" ]; then
    check_version
    install_videosdk_ubuntu
    install_xma_props_to_json_xrm
elif  [ "${OSNAME}" == "Amazon Linux" ]; then
    check_version
    install_videosdk_amazonlinux
    install_xma_props_to_json_xrm
else
    echo "This program [$0] does not support your operating system [${OSNAME}]"
fi

echo "##########################################################################################"
echo " "
echo " 1) Reboot is required to use the xilinx video driver"
echo " $ sudo reboot"
echo " "
echo " 2) The firmware of the U30 device must be updated. Once the update is complete, you must *cold boot.*"
echo "    If an error occurs, you must factory reset the device (https://xilinx.github.io/video-sdk/v3.0/card_management.html#recovering-a-card)"
echo " $ source /opt/xilinx/xrt/setup.sh"
echo " $ sudo /opt/xilinx/xrt/bin/xball --device-filter u30 xbmgmt program --base"
echo " $ sudo shutdown now"
echo " "
echo " 3) Rebooting is complete, configure the runtime environment and verify that the card has been detected."
echo "    You must always run this when using a new terminal."
echo " $ source /opt/xilinx/xcdr/setup.sh"
echo " $ xbutil examine"
echo " "
echo " 4) If you want to utilize the Xilinx Media Accelerator by registering OvenMediaEngine as a service, "
echo "    you'll need to modify the /lib/systemd/system/ovenmediaengine.service file to set up the runtime environment."
echo " $ sudo vi /lib/systemd/system/ovenmediaengine.service"
echo " "
echo "   ExecStart=/bin/bash -c 'source /opt/xilinx/xcdr/setup.sh -f; /usr/bin/OvenMediaEngine -d'"
echo " "
echo "##########################################################################################"
