# Realtime Speech-to-Text

OvenMediaEngine (OME) version 0.20.0 and later supports real-time automatic subtitles through integration with whisper.cpp. This feature converts live audio streams to text in real time and can optionally translate the recognized speech into English.

For real-time performance, an NVIDIA GPU is strongly recommended. While whisper.cpp can run on the CPU, it may result in latency or incomplete transcription.

<figure><img src="../.gitbook/assets/image.png" alt=""><figcaption></figcaption></figure>

## Prerequisites

### NVIDIA GPU and Driver

Check your GPU and driver status using:

```
$ nvidia-smi
Fri Oct 10 21:34:25 2025       
+-----------------------------------------------------------------------------------------+
| NVIDIA-SMI 580.82.09              Driver Version: 580.82.09      CUDA Version: 13.0     |
+-----------------------------------------+------------------------+----------------------+
| GPU  Name                 Persistence-M | Bus-Id          Disp.A | Volatile Uncorr. ECC |
| Fan  Temp   Perf          Pwr:Usage/Cap |           Memory-Usage | GPU-Util  Compute M. |
|                                         |                        |               MIG M. |
|=========================================+========================+======================|
|   0  NVIDIA GeForce GTX 1060 3GB    Off |   00000000:0A:00.0 Off |                  N/A |
| 53%   29C    P8              6W /  120W |     135MiB /   3072MiB |      0%      Default |
|                                         |                        |                  N/A |
+-----------------------------------------+------------------------+----------------------+

+-----------------------------------------------------------------------------------------+
| Processes:                                                                              |
|  GPU   GI   CI              PID   Type   Process name                        GPU Memory |
|        ID   ID                                                               Usage      |
|=========================================================================================|
|    0   N/A  N/A            1430      C   /usr/bin/OvenMediaEngine                 60MiB |
|    0   N/A  N/A            2017      G   /usr/lib/xorg/Xorg                       56MiB |
|    0   N/A  N/A            2232      G   /usr/bin/gnome-shell                      5MiB |
+-----------------------------------------------------------------------------------------+
```

If a driver is not installed, download it from the NVIDIA website or use the helper script provided in the OME repository.

Official driver: [https://www.nvidia.com/en-us/drivers/](https://www.nvidia.com/en-us/drivers/)

OME install script: [https://github.com/AirenSoft/OvenMediaEngine/blob/master/misc/install\_nvidia\_driver.sh](../../misc/install_nvidia_driver.sh)

{% hint style="warning" %}
The script installs the latest driver. Ensure your GPU supports the version being installed.
{% endhint %}

### CUDA Toolkit

CUDA Toolkit is required to build whisper.cpp with GPU acceleration.

* Download from: [https://developer.nvidia.com/cuda-downloads](https://developer.nvidia.com/cuda-downloads)
* Use a version that matches your GPU generation.
  * For example, GeForce 10xx series (e.g., GTX 1060) typically requires **CUDA 11.8**. Newer toolkits such as 13.x may not support older GPUs.

### Build and Install whisper.cpp

Run the latest prerequisites.sh script from the OME source root to build and install whisper.cpp.

```
$ ./misc/prerequisites.sh --enable-nv
```

## Configuration

Enable subtitles by using `<MediaOptions><Subtitles>`. For more details, refer to the [Subtitles](./) section. Each `<Rendition>` can include a `<Transcription>` element to enable speech-to-text.

Example configuration:

```xml
<OutputProfiles>
    <MediaOptions>
        <Subtitles>
            <Enable>true</Enable>
            <DefaultLabel>Origin</DefaultLabel>
            <Rendition>
                <Language>auto</Language>
                <Label>Origin</Label>
                <AutoSelect>true</AutoSelect>
                <Forced>false</Forced>
                <Transcription>
                    <Engine>whisper</Engine>
                    <Model>whisper_model/ggml-small.bin</Model>
                    <AudioIndexHint>0</AudioIndexHint>
                    <SourceLanguage>auto</SourceLanguage>
                    <Translation>false</Translation>
                </Transcription>
            </Rendition>
            <Rendition>
                <Language>en</Language>
                <Label>English</Label>
                <AutoSelect>true</AutoSelect>
                <Forced>false</Forced>
                <Transcription>
                    <Engine>whisper</Engine>
                    <Model>whisper_model/ggml-small.bin</Model>
                    <AudioIndexHint>0</AudioIndexHint>
                    <SourceLanguage>auto</SourceLanguage>
                    <Translation>true</Translation>
                </Transcription>
            </Rendition>
        </Subtitles>
    </MediaOptions>
```

The Transcription configuration includes the following options:

<table><thead><tr><th width="164">Key</th><th>Description</th></tr></thead><tbody><tr><td>Engine</td><td>The STT engine to use. Currently, only "whisper" is supported.</td></tr><tr><td>Model</td><td>Specifies the path to the whisper.cpp model file.</td></tr><tr><td>AudioIndexHint</td><td>Specifies the index of the audio track in the input stream. Default is 0</td></tr><tr><td>SourceLanguage</td><td>Specifies the language code of the input audio (ISO 639-1, e.g., ko, en, ja). Set to auto to enable automatic detection</td></tr><tr><td>Translation</td><td>When set to true, translates the recognized text into English. whisper currently supports translation to English only. If this is true, the resulting subtitle track language is automatically set to English (en)</td></tr></tbody></table>

### Model

The option specifies which whisper.cpp model is used for transcription. Model files can be downloaded from [https://huggingface.co/ggerganov/whisper.cpp](https://huggingface.co/ggerganov/whisper.cpp) For example, you can download a model with the following command:

```
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v2.bin
```

The model path can be set either as a relative path based on the configuration directory (where Server.xml is located) or as an absolute path starting with /. Smaller models such as ggml-small.bin provide faster performance but lower accuracy, while larger models like ggml-base.bin or ggml-large.bin offer higher accuracy at the cost of increased computation and memory usage.
