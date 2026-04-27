###########################################################################################################
#
# Dockerfile for OvenMediaEngine 
#
###########################################################################################################
#
# Default Arguments:
# 1. USE_LOCAL=false
# 2. USE_GPU=false
#
# Build the Docker image:
#
# 1. To build with the [latest source from GitHub]
#    $ docker build -t ovenmediaengine:dev -f Dockerfile .
#
# 2. To build using the [local source code]
#    $ docker build -t ovenmediaengine:dev -f Dockerfile --build-arg USE_LOCAL=true .
#
# 3. To build with NVIDIA/CUDA support
#    $ docker build -t ovenmediaengine:dev -f Dockerfile --build-arg USE_GPU=true .
#
# Run the Docker container:
#
# 1. Run container 
#    $ docker run -it ovenmediaengine:dev 
#    $ docker run -it -v $PWD/conf:/opt/ovenmediaengine/bin/origin_conf ovenmediaengine:dev 
#
# 2. Run container with NVIDIA/GPU support
#    $ docker run -it --gpus all ovenmediaengine:dev
#    $ docker run -it --gpus all -v $PWD/conf:/opt/ovenmediaengine/bin/origin_conf ovenmediaengine:dev 
#


###########################################################################################################
# Build Stage
###########################################################################################################
# Base Image Selection
ARG     USE_GPU
FROM    ubuntu:22.04 AS base
FROM    nvidia/cuda:12.0.1-devel-ubuntu22.04 AS base_gpu
FROM    base${USE_GPU:+_gpu} AS base_build

## Install Libraries 
ENV     DEBIAN_FRONTEND=noninteractive
RUN     apt-get update && apt-get install -y tzdata sudo curl git apt-transport-https ca-certificates gnupg software-properties-common wget build-essential ninja-build pkg-config

FROM    base_build AS build

WORKDIR /tmp

ARG     USE_GPU
ARG     OME_VERSION=master
ARG     USE_LOCAL=false
ARG     STRIP=true

ENV     PREFIX=/opt/ovenmediaengine
ENV     TEMP_DIR=/tmp/ome
ENV     TEMP_LOCAL_DIR=/tmp/ome_local

# Prepare Source Code
# - Copy local source to image (used only when USE_LOCAL=true)
COPY    . ${TEMP_LOCAL_DIR}
RUN \
        if [ "${USE_LOCAL}" = "true" ] || [ "${USE_LOCAL}" = "1" ] || [ "${USE_LOCAL}" = "yes" ]; then \
                rm -rf ${TEMP_DIR} && \
                mkdir -p ${TEMP_DIR} && \
                cp -a ${TEMP_LOCAL_DIR}/. ${TEMP_DIR}/; \
        else \
                rm -rf ${TEMP_DIR} && \
                git clone --branch ${OME_VERSION} --single-branch --depth 1 https://github.com/OvenMediaLabs/OvenMediaEngine ${TEMP_DIR}; \
        fi && \
        rm -rf ${TEMP_LOCAL_DIR}

## Install CMake
RUN \
        wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null && \
        apt-add-repository -y "deb https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" && \
        apt update && \
        apt install -y cmake

## Build OvenMediaEngine
#  - Configure ldconfig to find the cuda and nvml libraries 
RUN \
        build_options=""; \
        if [ "${USE_GPU}" = "true" ] || [ "${USE_GPU}" = "1" ] || [ "${USE_GPU}" = "yes" ]; then \
            build_options="-DOME_HWACCEL_NVIDIA=ON"; \
            echo -e "/usr/local/cuda/compat\n/usr/local/cuda/lib64/stubs" | tee /etc/ld.so.conf.d/cuda.conf > /dev/null && ldconfig; \
        fi; \
        cd ${TEMP_DIR} && \
        cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release ${build_options} && \
        cmake --build build/Release

RUN \
        if [ "${STRIP}" = "true" ] || [ "${STRIP}" = "1" ] || [ "${STRIP}" = "yes" ]; then \
                strip ${TEMP_DIR}/build/Release/bin/OvenMediaEngine ; \
        fi

## Copy Running Environment
RUN \
        cd ${TEMP_DIR} && \
        mkdir -p ${PREFIX}/bin/origin_conf && \
        mkdir -p ${PREFIX}/bin/edge_conf && \
        cp ./build/Release/bin/OvenMediaEngine ${PREFIX}/bin/ && \
        cp ./misc/conf_examples/Origin.xml ${PREFIX}/bin/origin_conf/Server.xml && \
        cp ./misc/conf_examples/Logger.xml ${PREFIX}/bin/origin_conf/Logger.xml && \
        cp ./misc/conf_examples/Edge.xml ${PREFIX}/bin/edge_conf/Server.xml && \
        cp ./misc/conf_examples/Logger.xml ${PREFIX}/bin/edge_conf/Logger.xml && \
        cp ./misc/ome_launcher.sh ${PREFIX}/bin/ome_launcher.sh

ENTRYPOINT ["tail", "-f", "/dev/null"]


###########################################################################################################
# Release Stage
###########################################################################################################
ARG     USE_GPU
FROM    ubuntu:22.04 AS release_base
FROM    nvidia/cuda:12.0.1-runtime-ubuntu22.04 AS release_base_gpu
FROM    release_base${USE_GPU:+_gpu} AS release

## Install libraries by package
ENV     DEBIAN_FRONTEND=noninteractive
RUN     apt-get update && apt-get install -y tzdata sudo libgomp1

WORKDIR         /opt/ovenmediaengine/bin
EXPOSE          80/tcp 8080/tcp 8090/tcp 1935/tcp 3333/tcp 3334/tcp 4000-4005/udp 10000-10010/udp 9000/tcp
COPY            --from=build /opt/ovenmediaengine /opt/ovenmediaengine

ENV     NVIDIA_VISIBLE_DEVICES=all
ENV     NVIDIA_DRIVER_CAPABILITIES=all
ENV     NVIDIA_REQUIRE_CUDA=cuda>=12.0

# Default run as Origin mode
CMD             ["/opt/ovenmediaengine/bin/ome_launcher.sh", "-c", "origin_conf"]
# ENTRYPOINT ["tail", "-f", "/dev/null"]

