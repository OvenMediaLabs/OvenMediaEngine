#!/bin/bash
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

echo ${OSTYPE} / ${OSNAME} / ${OSVERSION}.${OSMINORVERSION}
echo ${CURRENT}
##########################################################################################


install_base_ubuntu()
{
    # Install Docker 2.x
    curl https://get.docker.com && sudo systemctl --now enable docker

    # Install NVIDIA Docker container Toolkit
    distribution=$(. /etc/os-release;echo $ID$VERSION_ID) && \
    curl -s -L https://nvidia.github.io/nvidia-docker/gpgkey | sudo apt-key add - && \
    curl -s -L https://nvidia.github.io/nvidia-docker/$distribution/nvidia-docker.list | sudo tee /etc/apt/sources.list.d/nvidia-docker.list

    sudo apt-get update
    sudo apt-get install -y nvidia-docker2

    # Restart Docker
    sudo systemctl restart docker
}

install_base_centos()
{
    # Install Docker 2.x
    if [[ "${OSVERSION}" == "7" ]]; then
        sudo yum install -y yum-utils
        sudo yum-config-manager --add-repo=https://download.docker.com/linux/centos/docker-ce.repo
        sudo yum repolist -v
        sudo yum install -y https://download.docker.com/linux/centos/7/x86_64/stable/Packages/containerd.io-1.4.3-3.1.el7.x86_64.rpm
        sudo yum install docker-ce -y
        sudo systemctl --now enable docker
    elif [[ "${OSVERSION}" == "8" ]]; then
        sudo dnf install -y dnf-utils
        sudo dnf config-manager --add-repo=https://download.docker.com/linux/centos/docker-ce.repo
        sudo dnf repolist -v
        sudo dnf install -y https://download.docker.com/linux/centos/7/x86_64/stable/Packages/containerd.io-1.4.3-3.1.el7.x86_64.rpm
        sudo dnf install docker-ce -y
        sudo systemctl --now enable docker
    else
        fail_exit
    fi

    # Install NVIDIA Docker container Toolkit
    distribution=$(. /etc/os-release;echo $ID$VERSION_ID) && \
    curl -s -L https://nvidia.github.io/nvidia-docker/$distribution/nvidia-docker.repo | sudo tee /etc/yum.repos.d/nvidia-docker.repo

    if [[ "${OSVERSION}" == "7" ]]; then
        sudo yum clean expire-cache
        sudo yum install -y nvidia-docker2
    elif [[ "${OSVERSION}" == "8" ]]; then
        sudo dnf clean expire-cache --refresh
        sudo dnf install -y nvidia-docker2
    else
        fail_exit
    fi

    # Restart Docker
    sudo systemctl restart docker
}

test()
{
    # Test
    sudo docker run --rm --gpus all nvidia/cuda:11.8.0-base-ubuntu20.04 nvidia-smi 
}

fail_exit()
{
    echo "($1) installation has failed."
    cd ${CURRENT}
    exit 1
}

check_version()
{
    if [[ "${OSNAME}" == "Ubuntu" && "${OSVERSION}" != "18" && "${OSVERSION}" != "20" ]]; then
        proceed_yn
    fi

    if [[ "${OSNAME}" == "CentOS" && "${OSVERSION}" != "7" && "${OSVERSION}" != "8" ]]; then
        proceed_yn
    fi
}

proceed_yn()
{
    read -p "This program [$0] is tested on [Ubuntu 18/20.04, CentOS 7/8 q, Fedora 28]
Do you want to continue [y/N] ? " ANS
    if [[ "${ANS}" != "y" && "$ANS" != "yes" ]]; then
        cd ${CURRENT}
        exit 1
    fi
}

no_supported()
{
    echo "Docker containers are not supported on this platform"
    exit 1
}

if [ "${OSNAME}" == "Ubuntu" ]; then
    check_version
    install_base_ubuntu
    test
elif  [ "${OSNAME}" == "CentOS" ]; then
    check_version
    install_base_centos
    test
else
    echo "This program [$0] does not support your operating system [${OSNAME}]"
    echo "Please refer to manual installation page"
fi
