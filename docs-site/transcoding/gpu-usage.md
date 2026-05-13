---
title: GPU Acceleration
sidebar_position: 22
---

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

OvenMediaEngine supports GPU-based hardware decoding and encoding. Currently supported GPU acceleration devices are Intel's QuickSync and NVIDIA. This article explains how to install the drivers for OvenMediaEngine and set up the configuration to use your GPU.

## 1. Install Drivers


<Tabs>
<TabItem value="nvidia" label="NVIDIA">

#### 1. Install NVIDIA GPU Driver

If you are using an NVIDIA graphics card, please refer to the following guide to install the driver. The OS that supports installation with the provided script are **CentOS 7/8** and **Ubuntu 18/20** versions. If you want to install the driver in another OS, please refer to the manual installation guide document.

CentOS environment requires the process of uninstalling the nouveau driver. After uninstalling the driver, the first reboot is required, and a new NVIDIA driver must be installed and rebooted. Therefore, two install scripts must be executed.


```bash
(curl -LOJ https://github.com/OvenMediaLabs/OvenMediaEngine/archive/master.tar.gz && tar xvfz OvenMediaEngine-master.tar.gz)
OvenMediaEngine-master/misc/install_nvidia_driver.sh
```


**How to check driver installation**

After the driver installation is complete, check whether the driver is operating normally with the nvidia-smi command.

```bash
$ nvidia-smi
+---------------------------------------------------------------------------------------+
| NVIDIA-SMI 535.288.01             Driver Version: 535.288.01   CUDA Version: 12.2     |
|-----------------------------------------+----------------------+----------------------+
| GPU  Name                 Persistence-M | Bus-Id        Disp.A | Volatile Uncorr. ECC |
| Fan  Temp   Perf          Pwr:Usage/Cap |         Memory-Usage | GPU-Util  Compute M. |
|                                         |                      |               MIG M. |
|=========================================+======================+======================|
|   0  NVIDIA GeForce GTX 1050        On  | 00000000:3B:00.0  On |                  N/A |
| 20%   39C    P8              N/A /  75W |    204MiB /  2048MiB |      0%      Default |
|                                         |                      |                  N/A |
+-----------------------------------------+----------------------+----------------------+
|   1  NVIDIA RTX 4000 SFF Ada ...    On  | 00000000:AF:00.0 Off |                  Off |
| 30%   38C    P8               5W /  70W |    171MiB / 20475MiB |      0%      Default |
|                                         |                      |                  N/A |
+-----------------------------------------+----------------------+----------------------+
                                                                                         
+---------------------------------------------------------------------------------------+
| Processes:                                                                            |
|  GPU   GI   CI        PID   Type   Process name                            GPU Memory |
|        ID   ID                                                             Usage      |
|=======================================================================================|
|    0   N/A  N/A    802940      C   ...prise/src/bin/DEBUG/OvenMediaEngine       40MiB |
|    1   N/A  N/A    802940      C   ...prise/src/bin/DEBUG/OvenMediaEngine      158MiB |
+---------------------------------------------------------------------------------------+
```

#### 2 . Prerequisites

If you have finished installing the driver to use the GPU, you need to reinstall the open source library using Prerequisites.sh . The purpose is to allow external libraries to use the installed graphics driver.

```bash
OvenMediaEngine-master/misc/prerequisites.sh --enable-nv
```

</TabItem>
<TabItem value="nvidia-with-docker" label="NVIDIA with Docker">

#### 1. Install NVIDIA GPU Driver

Please refer to the NVIDIA Driver installation guide written previously.

#### 2. Install NVIDIA Container Toolkit

To use GPU acceleration in Docker, you need to install NVIDIA drivers on your host OS and install the NVIDIA Container Toolkit. This toolkit includes container runtime libraries and utilities for using NVIDIA GPUs in Docker containers.

```bash
OvenMediaEngine-master/misc/install_nvidia_docker_container.sh
```

#### 3 . Build Image

A Docker Image build script that supports NVIDIA GPU is provided separately. Please refer to the previous guide for how to build

```
OvenMediaEngine-master/Dockerfile.cuda
OvenMediaEngine-master/Dockerfile.cuda.local
```

</TabItem>
<TabItem value="netint-vpu-ni-logan" label="Netint VPU Ni Logan ">

#### 1. Install XCODER

Please refer to the Netint documentation to install XCODER.

**How to check driver installation**

After the driver installation is complete, check if the libxcoder exist: the CLI must return something like `libxcoder_logan.so (libc6,x86-64) => /usr/local/lib/libxcoder_logan.so`

```bash
ldconfig -p | grep libxcoder_logan.so
```

#### 2. Prerequisites

If you have finished installing the driver to use the VPU, you need to reinstall the open source library using Prerequisites.sh . The purpose is to allow external libraries to use the installed graphics driver. You also have to unzip the ffmpeg patch provide by netint in a specfic path

#### Using Netint VPU

```bash
./prerequisites.sh --enable-nilogan --nilogan-path=/root/T4xx/release/FFmpeg-n5.0_t4xx_patch
```

</TabItem>
</Tabs>


## 2. Build & Run

Please refer to the link for how to build and run.


[getting-started](../getting-started/README.md)



:::info

**Intructions on running Docker**

you must include the **--gpus all** option when running Docker


```
<strong>docker run -d ... --gpus all ovenmedialabs/ovenmediaengine:dev
</strong>
```


:::


## 3. Configuration

To use hardware acceleration, set the **HardwareAcceleration** option to **true** under OutputProfiles. If this option is enabled, a hardware codec is automatically used when creating a stream, and if it is unavailable due to insufficient hardware resources, it is replaced with a software codec.

```markup
<OutputProfiles>
    <HWAccels>
        <!-- 
        Setting for Hardware Modules.
            - nv : Nvidia Video Codec SDK
            - xma :Xilinx Media Accelerator
            - nilogan: Netint VPU

        You can use multiple modules by separating them with commas.
        For example, if you want to use xma and nv, you can set it as follows.

        <Modules>[ModuleName]:[DeviceId],[ModuleName]:[DeviceId],...</Modules>
         <Modules>nv:0,nv:1,xma:0</Modules>
        -->
        <Decoder>
                <Enable>true</Enable>
                <Modules>nv</Modules>
        </Decoder>
        <Encoder>
                <Enable>true</Enable>
                <Modules>nv</Modules>
        </Encoder>
    </HWAccels>
    
    <OutputProfile>
        ...
    </OutputProfile>
</OutputProfiles>
```


[configuration](../configuration/README.md)


## Appendix. Support Format

The codecs available using hardware accelerators in OvenMediaEngine are as shown in the table below. Different GPUs support different codecs. If the hardware codec is not available, you should check if your GPU device supports the codec.

<table data-full-width="false"><thead><tr><th width="199">Device</th><th width="150" align="center">H264</th><th width="141" align="center">H265</th><th width="126" align="center">VP8</th><th align="center">VP9</th></tr></thead><tbody><tr><td>NVIDIA</td><td align="center">D / E</td><td align="center">D / E</td><td align="center">-</td><td align="center">-</td></tr><tr><td>Docker on NVIDIA Container Toolkit</td><td align="center">D / E</td><td align="center">D / E</td><td align="center">-</td><td align="center">-</td></tr><tr><td>Xilinx U30MA</td><td align="center">D / E</td><td align="center">D / E</td><td align="center"></td><td align="center"></td></tr></tbody></table>

D : Decoding, E : Encoding

## Reference

* NVIDIA NVDEC Video Format : [https://en.wikipedia.org/wiki/Nvidia\_NVDEC](https://en.wikipedia.org/wiki/Nvidia_NVDEC)
* NVIDIA NVENV Video Format : [https://en.wikipedia.org/wiki/Nvidia\_NVENC](https://en.wikipedia.org/wiki/Nvidia_NVENC)
* CUDA Toolkit Installation Guide : [https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#introduction](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#introduction)
* NVIDIA Container Toolkit : [https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/arch-overview.html#arch-overview](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/arch-overview.html#arch-overview)
* Xilinx Video SDK : [https://xilinx.github.io/video-sdk/v3.0/index.html](https://xilinx.github.io/video-sdk/v3.0/index.html)
