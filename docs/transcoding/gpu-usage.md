# GPU Acceleration

OvenMediaEngine supports GPU-based hardware decoding and encoding. Currently supported GPU acceleration devices are Intel's QuickSync and NVIDIA. This article explains how to install the drivers for OvenMediaEngine and set up the configuration to use your GPU.

## 1. Install Drivers

{% tabs %}
{% tab title="NVIDIA" %}
#### 1. Install NVIDIA GPU Driver

If you are using an NVIDIA graphics card, please refer to the following guide to install the driver. The OS that supports installation with the provided script are **CentOS 7/8** and **Ubuntu 18/20** versions. If you want to install the driver in another OS, please refer to the manual installation guide document.

CentOS environment requires the process of uninstalling the nouveau driver. After uninstalling the driver, the first reboot is required, and a new NVIDIA driver must be installed and rebooted. Therefore, two install scripts must be executed.

{% code overflow="wrap" lineNumbers="true" %}
```bash
(curl -LOJ https://github.com/AirenSoft/OvenMediaEngine/archive/master.tar.gz && tar xvfz OvenMediaEngine-master.tar.gz)
OvenMediaEngine-master/misc/install_nvidia_driver.sh
```
{% endcode %}

**How to check driver installation**

After the driver installation is complete, check whether the driver is operating normally with the nvidia-smi command.

```bash
$ nvidia-smi

Thu Jun 17 10:20:23 2021
+-----------------------------------------------------------------------------+
| NVIDIA-SMI 465.19.01    Driver Version: 465.19.01    CUDA Version: 11.3     |
|-------------------------------+----------------------+----------------------+
| GPU  Name        Persistence-M| Bus-Id        Disp.A | Volatile Uncorr. ECC |
| Fan  Temp  Perf  Pwr:Usage/Cap|         Memory-Usage | GPU-Util  Compute M. |
|                               |                      |               MIG M. |
|===============================+======================+======================|
|   0  NVIDIA GeForce ...  Off  | 00000000:01:00.0 Off |                  N/A |
| 20%   35C    P8    N/A /  75W |    156MiB /  1997MiB |      0%      Default |
|                               |                      |                  N/A |
+-------------------------------+----------------------+----------------------+
```

#### 2 . Prerequisites

If you have finished installing the driver to use the GPU, you need to reinstall the open source library using Prerequisites.sh . The purpose is to allow external libraries to use the installed graphics driver.

```bash
OvenMediaEngine-master/misc/prerequisites.sh --enable-nvc
```
{% endtab %}

{% tab title="NVIDIA with Docker" %}
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
{% endtab %}

{% tab title="Netint VPU Ni Logan" %}
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
{% endtab %}
{% endtabs %}

## 2. Build & Run

Please refer to the link for how to build and run.

{% content-ref url="../getting-started/" %}
[getting-started](../getting-started/)
{% endcontent-ref %}

{% hint style="info" %}
**Intructions on running Docker**

you must include the **--gpus all** option when running Docker

<pre data-overflow="wrap"><code><strong>docker run -d ... --gpus all airensoft/ovenmediaengine:dev
</strong></code></pre>
{% endhint %}

## 3. Configuration

To use hardware acceleration, set the **HardwareAcceleration** option to **true** under OutputProfiles. If this option is enabled, a hardware codec is automatically used when creating a stream, and if it is unavailable due to insufficient hardware resources, it is replaced with a software codec.

```markup
<OutputProfiles>
    <HWAccels>
        <!-- 
        Setting for Hardware Modules.
            - nv : Nvidia Video Codec SDK
            - xma :Xilinx Media Accelerator
            - qsv :Intel Quick Sync Video
            - nilogan: Netint VPU

        You can use multiple modules by separating them with commas.
        For example, if you want to use xma and nv, you can set it as follows.

        <Modules>[ModuleName]:[DeviceId],[ModuleName]:[DeviceId],...</Modules>
        <Modules>xma:0,nv:0</Modules>
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

{% content-ref url="../configuration/" %}
[configuration](../configuration/)
{% endcontent-ref %}

## Appendix. Support Format

The codecs available using hardware accelerators in OvenMediaEngine are as shown in the table below. Different GPUs support different codecs. If the hardware codec is not available, you should check if your GPU device supports the codec.

<table><thead><tr><th>Device</th><th width="199" align="center">H264</th><th align="center">H265</th><th align="center">VP8</th><th align="center">VP9</th></tr></thead><tbody><tr><td>QuickSync</td><td align="center">D / E</td><td align="center">D / E</td><td align="center">-</td><td align="center">-</td></tr><tr><td>NVIDIA</td><td align="center">D / E</td><td align="center">D / E</td><td align="center">-</td><td align="center">-</td></tr><tr><td>Docker on NVIDIA Container Toolkit</td><td align="center">D / E</td><td align="center">D / E</td><td align="center">-</td><td align="center">-</td></tr><tr><td>Xilinx U30MA</td><td align="center">D / E</td><td align="center">D / E</td><td align="center"></td><td align="center"></td></tr></tbody></table>

D : Decoding, E : Encoding

## Reference

* NVIDIA NVDEC Video Format : [https://en.wikipedia.org/wiki/Nvidia\_NVDEC](https://en.wikipedia.org/wiki/Nvidia_NVDEC)
* NVIDIA NVENV Video Format : [https://en.wikipedia.org/wiki/Nvidia\_NVENC](https://en.wikipedia.org/wiki/Nvidia_NVENC)
* CUDA Toolkit Installation Guide : [https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#introduction](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#introduction)
* NVIDIA Container Toolkit : [https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/arch-overview.html#arch-overview](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/arch-overview.html#arch-overview)
* Quick Sync Video format support: [https://en.wikipedia.org/wiki/Intel\_Quick\_Sync\_Video](https://en.wikipedia.org/wiki/Intel_Quick_Sync_Video#AMD)
* Xilinx Video SDK : [https://xilinx.github.io/video-sdk/v3.0/index.html](https://xilinx.github.io/video-sdk/v3.0/index.html)

##
