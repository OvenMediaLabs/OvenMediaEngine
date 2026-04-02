# Realtime Speech-to-Text

OvenMediaEngine (OME) version 0.20.0 and later supports real-time automatic subtitles through integration with whisper.cpp. This feature converts live audio streams to text in real time and can optionally translate the recognized speech into English.

An NVIDIA GPU is required. CPU inference is not supported because it is too slow for real-time live transcription.

<figure><img src="../.gitbook/assets/image.png" alt=""><figcaption></figcaption></figure>

## Prerequisites

### NVIDIA GPU and Driver

Check your GPU and driver status using:

```
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

If a driver is not installed, download it from the NVIDIA website or use the helper script provided in the OME repository.

Official driver: [https://www.nvidia.com/en-us/drivers/](https://www.nvidia.com/en-us/drivers/)

OME install script: [https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/misc/install\_nvidia\_driver.sh](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/misc/install_nvidia_driver.sh)

{% hint style="warning" %}
This script installs the versions recommended by OME. If you want to install the latest version, change the parameters.
{% endhint %}

### CUDA Toolkit

CUDA Toolkit is required to build whisper.cpp with GPU acceleration.

* Download from: [https://developer.nvidia.com/cuda-downloads](https://developer.nvidia.com/cuda-downloads)
* Use a version that matches your GPU generation.
  * Recommended CUDA Toolkit : v12.0 \~ v12.8

### Build and Install whisper.cpp

Run the latest prerequisites.sh script from the OME source root to build and install whisper.cpp.

```
$ ./misc/prerequisites.sh --enable-nv
```

## Configuration

STT configuration is split across two sections:

* **`<Modules><Whisper>`** in `Server.xml` — preloads model files into GPU memory at startup.
* **`<Application><Subtitles>`** — defines subtitle renditions (label, language, etc.) that STT output will be written to.
* **`<Application><OutputProfiles><MediaOptions><STT>`** — connects an input audio track to a subtitle rendition via an STT engine.

{% hint style="warning" %}
**Breaking change:** The `<Transcription>` element inside `<Subtitles><Rendition>` has been removed. If your existing configuration uses `<Subtitles><Rendition><Transcription>`, it will no longer work. Please migrate to `<OutputProfiles><MediaOptions><STT><Rendition>` as described below.
{% endhint %}

### Step 1: Preload Models (Server.xml)

Declare the Whisper model files to load at server startup inside `<Modules><Whisper>`. Multiple `<PreloadModel>` entries are allowed. Models are loaded in descending file-size order to maximize GPU utilization.

```xml
<Server>
    <Modules>
        <Whisper>
            <PreloadModel>whisper_model/ggml-small.bin</PreloadModel>
            <PreloadModel>whisper_model/ggml-medium.bin</PreloadModel>
        </Whisper>
    </Modules>
</Server>
```

{% hint style="info" %}
`<PreloadModel>` is optional. If omitted, models are loaded on demand when the first stream that uses them is published. Preloading is recommended for production to avoid a delay on the first stream.
{% endhint %}

### Step 2: Define Subtitle Renditions

Define the subtitle tracks that will receive STT output. For more details on `<Subtitles>`, refer to the [Subtitles](./) section.

```xml
<Application>
    <Subtitles>
        <Enable>true</Enable>
        <DefaultLabel>Korean</DefaultLabel>
        <Rendition>
            <Language>ko</Language>
            <Label>Korean</Label>
            <AutoSelect>true</AutoSelect>
            <Forced>false</Forced>
        </Rendition>
        <Rendition>
            <Language>en</Language>
            <Label>English</Label>
        </Rendition>
    </Subtitles>
</Application>
```

### Step 3: Configure STT in OutputProfiles

Under `<OutputProfiles><MediaOptions><STT>`, add a `<Rendition>` for each audio-to-subtitle mapping. The `<OutputSubtitleLabel>` must match a `<Label>` defined in `<Subtitles>`.

```xml
<Application>
    <OutputProfiles>
        <MediaOptions>
            <STT>
                <Rendition>
                    <Engine>whisper</Engine>
                    <Model>whisper_model/ggml-small.bin</Model>
                    <InputAudioIndex>0</InputAudioIndex>
                    <OutputSubtitleLabel>Korean</OutputSubtitleLabel>
                    <SourceLanguage>auto</SourceLanguage>
                    <Translation>false</Translation>
                </Rendition>
                <Rendition>
                    <Engine>whisper</Engine>
                    <Model>whisper_model/ggml-small.bin</Model>
                    <InputAudioIndex>0</InputAudioIndex>
                    <OutputSubtitleLabel>English</OutputSubtitleLabel>
                    <SourceLanguage>auto</SourceLanguage>
                    <Translation>true</Translation>
                </Rendition>
            </STT>
        </MediaOptions>
    </OutputProfiles>
</Application>
```

The `<STT><Rendition>` configuration includes the following options:

<table><thead><tr><th width="192">Key</th><th>Description</th></tr></thead><tbody><tr><td>Engine</td><td>The STT engine to use. Currently, only <code>whisper</code> is supported.</td></tr><tr><td>Model</td><td>Path to the whisper.cpp model file. Can be absolute or relative to the configuration directory (where Server.xml is located).</td></tr><tr><td>InputAudioIndex</td><td>Index of the audio track in the input stream to transcribe. Default is <code>0</code> (first audio track).</td></tr><tr><td>OutputSubtitleLabel</td><td>Label of the subtitle rendition (defined in <code>&lt;Subtitles&gt;</code>) to write the transcription output to.</td></tr><tr><td>SourceLanguage</td><td>Language code of the input audio (ISO 639-1, e.g., <code>ko</code>, <code>en</code>, <code>ja</code>). Set to <code>auto</code> to enable automatic detection.</td></tr><tr><td>Translation</td><td>When set to <code>true</code>, translates the recognized text into English. Whisper currently supports translation to English only.</td></tr></tbody></table>

### Model

Model files can be downloaded from [https://huggingface.co/ggerganov/whisper.cpp](https://huggingface.co/ggerganov/whisper.cpp). For example:

```
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v2.bin
```

Smaller models such as `ggml-small.bin` provide faster inference with lower accuracy. Larger models like `ggml-medium.bin` or `ggml-large.bin` offer higher accuracy at the cost of increased GPU memory and computation time.
