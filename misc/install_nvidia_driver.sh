#!/bin/bash

NVIDIA_DRIVER_VERSION=

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
CURRENT=$(pwd)


echo "##########################################################################################"
echo " Install NVIDIA drivers and CUDA Toolkit"
echo "##########################################################################################"
echo ${OSTYPE} / ${OSNAME} / ${OSVERSION} . ${OSMINORVERSION}


success_exit()
{
   # Configure Envionment Variables
    echo ""
    echo "Please add the PATH below to the environment variable."
    echo ""
    echo "   export PATH=\$PATH:/usr/local/cuda/bin/"
    echo ""
    echo "Reboot is required to use the nvidia video driver"
    export PATH=${PATH}:/usr/local/cuda/bin/
    exit 0
}

fail_exit()
{
    echo "This program [$0] does not support your operating system [${OSNAME} ${OSVERSION}]"
    echo "Please refer to manual installation page"
    exit 1
}

##########################################################################################
# Drivers for Ubuntu 18.04 / 20.04 / 22.04
##########################################################################################
install_base_ubuntu()
{
    if [ "${OSVERSION}" == "18" ] || [ "${OSVERSION}" == "20" ] || [ "${OSVERSION}" == "22" ]; then
        sudo apt-get -y update
        sudo apt-get -y install --no-install-recommends apt-utils lshw
        sudo apt-get -y install --no-install-recommends keyboard-configuration
        sudo apt-get -y install --no-install-recommends ubuntu-drivers-common
        sudo apt-get -y install --no-install-recommends gnupg2 ca-certificates software-properties-common

        # Uninstalling a previously installed NVIDIA Driver
        sudo apt-get -y remove --purge nvidia-*
        sudo apt-get -y autoremove
        sudo apt-get -y update

        # Remove the nouveau driver.
        # If the nouveau driver is in use, the nvidia driver cannot be installed.
        USE_NOUVEAU=`sudo lshw -class video | grep nouveau`
        if [ ! -z "$USE_NOUVEAU" ]; then

                # Disable nouveau Driver
                echo "blacklist nouveau" >> /etc/modprobe.d/blacklist.conf
                echo "blacklist lbm-nouveau" >> /etc/modprobe.d/blacklist.conf
                echo "options nouveau modeset=0" >> /etc/modprobe.d/blacklist.conf
                echo "alias nouveau off" >> /etc/modprobe.d/blacklist.conf
                echo "alias lbm-nouveau off" >> /etc/modprobe.d/blacklist.conf
                sudo update-initramfs -u
                echo "Using a driver display nouveau.Remove the driver and reboot.Reboot and installation script to rerun the nvidia display the driver to complete the installation."

                sleep 5s
                reboot
        fi

        # Install nvidia drivers and cuda-toolit
        sudo add-apt-repository -y  ppa:graphics-drivers/ppa

        sudo apt -y update
        if [ -z "$NVIDIA_DRIVER_VERSION" ]
        then 
            # Insall recommended version 470
            sudo apt-get install -y --no-install-recommends nvidia-driver-470
			# Install CUDA Toolkit 11.8
			sudo curl -O https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
			sudo dpkg -i cuda-keyring_1.1-1_all.deb
			sudo apt-get update
			sudo apt-get -y install cuda-toolkit-11-8
        else
            # installation with specific version
            sudo apt-get install -y --no-install-recommends nvidia-driver-${NVIDIA_DRIVER_VERSION}
			sudo apt-get install -y --no-install-recommends nvidia-cuda-toolkit
        fi
		
        success_exit
    else
        fail_exit
    fi
}

##########################################################################################
# Drivers for CentOS 7
##########################################################################################
install_base_centos()
{
    if [ "${OSVERSION}" == "7" ]; then
        sudo yum -y update
        sudo yum -y install kernel-devel
        sudo yum -y install epel-release
        sudo yum -y install dkms curl lshw
        sudo yum -y install subscription-manager

        echo "Reboot is required to run with a new version of the kernel."

        # Remove the nouveau driver.
        USE_NOUVEAU=`lshw -class video | grep nouveau`
        if [ ! -z "$USE_NOUVEAU" ]; then

                # Disable nouveau Driver
                sudo sed "s/GRUB_CMDLINE_LINUX=\"\(.*\)\"/GRUB_CMDLINE_LINUX=\"\1 rd.driver.blacklist=nouveau nouveau.modeset=0\"/" /etc/default/grub
                sudo grub2-mkconfig -o /boot/grub2/grub.cfg
                sudo echo "blacklist nouveau" >> /etc/modprobe.d/blacklist.conf
                sudo mv /boot/initramfs-$(uname -r).img /boot/initramfs-$(uname -r)-nouveau.img
                sudo dracut /boot/initramfs-$(uname -r).img $(uname -r)

                echo "Using a driver display nouveau. so, remove the driver and reboot. "
                echo "After reboot and installation script to rerun the nvidia display the driver to complete the installation."

                sleep 5s
                sudo reboot
        fi

        sudo subscription-manager repos --enable=rhel-7-server-optional-rpms
        sudo yum-config-manager --add-repo https://developer.download.nvidia.com/compute/cuda/repos/rhel7/x86_64/cuda-rhel7.repo
        sudo yum clean expire-cache

        sudo yum -y install nvidia-driver-latest-dkms
        sudo yum -y install cuda
        sudo yum -y install cuda-drivers

        success_exit
    else
        fail_exit
    fi
}

##########################################################################################
# Drivers for Rocky 8/9
##########################################################################################
install_base_rocky()
{
    sudo dnf update -y
    sudo dnf install epel-release -y
    sudo dnf groupinstall "Development Tools" -y
    sudo dnf install kernel-devel kernel-headers -y

    if [ "${OSVERSION}" == "8" ]; then 
        sudo dnf config-manager --add-repo=https://developer.download.nvidia.com/compute/cuda/repos/rhel8/x86_64/cuda-rhel8.repo
    elif [ "${OSVERSION}" == "9" ]; then 
        sudo dnf config-manager --add-repo=https://developer.download.nvidia.com/compute/cuda/repos/rhel9/x86_64/cuda-rhel9.repo
    else
        fail_exit
    fi

    sudo dnf clean all

    # Remove NVIDIA Driver
    # sudo dnf remove "cuda*" "*cublas*" "*cufft*" "*cufile*" "*curand*" "*cusolver*" "*cusparse*" "*gds-tools*" "*npp*" "*nvjpeg*" "nsight*" "*nvvm*"
    # sudo dnf module remove --all nvidia-driver
    # sudo dnf module reset nvidia-driver

    # List available Nvidia driver packages to confirm availability
    # sudo dnf list available nvidia-driver\* --showduplicates

    # Remove the nouveau driver.
    USE_NOUVEAU=`lshw -class video | grep nouveau`
    if [ ! -z "$USE_NOUVEAU" ]; then
        sudo bash -c 'echo "blacklist nouveau" > /etc/modprobe.d/blacklist-nouveau.conf'
        sudo bash -c 'echo "options nouveau modeset=0" >> /etc/modprobe.d/blacklist-nouveau.conf'
        sudo dracut --force
        echo "Nouveau driver has been blacklisted. Rebooting the system..."
        sudo reboot
    fi

    if [ -z "$NVIDIA_DRIVER_VERSION" ]; then 
        # installation with recommended version
        sudo dnf module install nvidia-driver:latest-dkms -y
    else
        # installation with specific version
        sudo dnf module install nvidia-driver:${NVIDIA_DRIVER_VERSION} -y
    fi     

    # Install CUDA toolkit 
    sudo dnf install cuda-toolkit-11-8 -y
 
    success_exit
}


# Arguments parser
while (($#)); do
    OPT=$1
    shift
    case $OPT in
        --*) case ${OPT:2} in
            nvidia_driver) NVIDIA_DRIVER_VERSION=$1; shift ;;
        esac;;
        -*) case ${OPT:1} in
            n) NVIDIA_DRIVER_VERSION=$1; shift;;
        esac;;
    esac
done

if [ "${OSNAME}" == "Ubuntu" ]; then
    install_base_ubuntu
elif  [ "${OSNAME}" == "CentOS" ]; then
    install_base_centos
elif  [ "${OSNAME}" == "Rocky" ]; then
    install_base_rocky
else
    fail_exit
fi
