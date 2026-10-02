---
title: Realtime Speech-to-Text
description: "Generate live subtitles for OvenMediaEngine streams with real-time speech-to-text on the CPU."
sidebar_position: 34
---

OvenMediaEngine (OME) version 0.20.0 and later supports real-time automatic subtitles through integration with whisper.cpp. This feature converts live audio streams to text in real time and can optionally translate the recognized speech into English.

Transcription runs on the CPU, so no GPU is required. How many streams a server can transcribe depends on the model you choose and how many CPU cores you can spare — see [Choosing a Model](#choosing-a-model).

![](../images/realtime-speech-to-text.png)

## Prerequisites

### Build and Install whisper.cpp

Install the prerequisites (including `whisper.cpp`) from the OME source root:

```
$ cmake -P cmake/InstallPrerequisites.cmake
```

By default `whisper.cpp` is built for a portable x86-64 baseline (AVX2/FMA/F16C, Haswell and
newer), so the resulting binary runs on any reasonably modern server. If you build OME on the
same machine you run it on, you can trade that portability for speed:

```
$ cmake -DOME_WHISPER_NATIVE=ON -P cmake/InstallPrerequisites.cmake
```

`OME_WHISPER_NATIVE=ON` compiles ggml with `-march=native`, which is noticeably faster on CPUs
with AVX-512 or AMX. The binary then only runs on CPUs that support the same instructions, so do
not use it for packages you distribute to other machines.

## Configuration

STT configuration is split across two sections:

* **`<Modules><Whisper>`** in `Server.xml` — preloads model files at startup and caps the total inference thread usage.
* **`<Application><Subtitles>`** — defines subtitle renditions (label, language, etc.) that STT output will be written to.
* **`<Application><OutputProfiles><MediaOptions><STT>`** — connects an input audio track to a subtitle rendition via an STT engine.


:::warning

**Breaking change:** The `<Transcription>` element inside `<Subtitles><Rendition>` has been removed. If your existing configuration uses `<Subtitles><Rendition><Transcription>`, it will no longer work. Please migrate to `<OutputProfiles><MediaOptions><STT><Rendition>` as described below.

:::


### Step 1: Preload Models (Server.xml)

Declare the Whisper model files to load at server startup inside `<Modules><Whisper>`. Multiple `<PreloadModel>` entries are allowed. Models are loaded in descending file-size order.

`<Modules><Whisper>` has the following fields:

| Key | Description |
|---|---|
| PreloadModel | A model to load at startup. Repeatable. |
| PreloadModel > Path | Path to the model file. Can be absolute or relative to the config directory. |
| MaxThreads | CPU threads Whisper may use for inference across every STT track on this server. If omitted or `0`, the number of hardware threads is used. Active tracks share this budget equally, each keeping at least one thread, so with more tracks than threads the total can exceed it. Lower it to reserve cores for transcoding. |

```xml
<Server>
    <Modules>
        <Whisper>
            <!-- Keep 8 of the machine's threads for transcription at most -->
            <MaxThreads>8</MaxThreads>

            <PreloadModel>
                <Path>whisper_model/ggml-small.bin</Path>
            </PreloadModel>
            <PreloadModel>
                <Path>whisper_model/ggml-base.en.bin</Path>
            </PreloadModel>
        </Whisper>
    </Modules>
</Server>
```

:::info

The `<Devices>` element used to pick a GPU to preload onto. It is still accepted so existing
configurations keep working, but it is ignored and logs a warning.

:::


:::info

`<PreloadModel>` is optional. If omitted, models are loaded on demand when the first stream that uses them is published. Preloading is recommended for production to avoid a delay on the first stream.

:::


### Step 2: Define Subtitle Renditions

Define the subtitle tracks that will receive STT output. For more details on `<Subtitles>`, refer to the [Subtitles](./README.md) section.

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
                <!-- Korean STT -->
                <Rendition>
                    <Engine>whisper</Engine>
                    <Model>whisper_model/ggml-small.bin</Model>
                    <InputAudioIndex>0</InputAudioIndex>
                    <OutputSubtitleLabel>Korean</OutputSubtitleLabel>
                    <SourceLanguage>auto</SourceLanguage>
                    <Translation>false</Translation>
                    <!-- Optional: CPU threads for this rendition -->
                    <Threads>4</Threads>
                    <!-- Optional: sliding-window tuning -->
                    <StepMs>2000</StepMs>
                    <LengthMs>10000</LengthMs>
                    <KeepMs>1500</KeepMs>
                </Rendition>
                <!-- English translation of the same audio track -->
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

<table><thead><tr><th width="192">Key</th><th>Description</th></tr></thead><tbody><tr><td>Engine</td><td>The STT engine to use. Currently, only `whisper` is supported.</td></tr><tr><td>Model</td><td>Path to the whisper.cpp model file. Can be absolute or relative to the configuration directory (where Server.xml is located).</td></tr><tr><td>InputAudioIndex</td><td>Index of the audio track in the input stream to transcribe. Default is `0` (first audio track).</td></tr><tr><td>OutputSubtitleLabel</td><td>Label of the subtitle rendition (defined in `&lt;Subtitles&gt;`) to write the transcription output to.</td></tr><tr><td>SourceLanguage</td><td>Language code of the input audio (ISO 639-1, e.g., `ko`, `en`, `ja`). Set to `auto` to enable automatic detection.</td></tr><tr><td>Translation</td><td>When set to `true`, translates the recognized text into English. Whisper currently supports translation to English only.</td></tr><tr><td>StepMs</td><td>How many milliseconds of new audio to collect before running each inference call. Default is `2000`. Lower values reduce subtitle latency but increase CPU load.</td></tr><tr><td>LengthMs</td><td>Total size of the audio window (in milliseconds) passed to Whisper per inference call. Default is `10000`. Larger windows give the model more context and improve accuracy.</td></tr><tr><td>KeepMs</td><td>Amount of audio (in milliseconds) carried over from the previous window after a context reset. Default is `1500`. Helps avoid cut-off words at window boundaries.</td></tr><tr><td>Threads</td><td>Upper bound on the CPU threads this rendition uses for inference. If omitted or `0`, a default derived from the number of hardware threads is used. The rendition actually gets this value or its equal share of `&lt;Modules&gt;&lt;Whisper&gt;&lt;MaxThreads&gt;` among the active STT tracks, whichever is smaller.</td></tr><tr><td>Modules</td><td>Deprecated. It used to select a GPU (e.g. `nv:0`). Whisper runs on the CPU, so the value is accepted for compatibility, ignored, and logged as a warning.</td></tr></tbody></table>

### Model

Model files can be downloaded from [https://huggingface.co/ggerganov/whisper.cpp](https://huggingface.co/ggerganov/whisper.cpp). For example:

```
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin
$ wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin
```

The `.en` variants are English-only and run faster than the multilingual model of the same size.
Use a multilingual model (`ggml-small.bin` and friends) only when the audio is not English or when
`<SourceLanguage>auto</SourceLanguage>` has to detect it.

### Choosing a Model

Inference has to finish within `<StepMs>` or subtitles fall behind the live audio. Larger models
are more accurate but need more CPU, so the model and the thread count have to be picked together.

| Model | Size | Threads per stream | Notes |
|---|---|---|---|
| tiny / tiny.en | 75 MB | 2 | Lowest accuracy. Many concurrent streams per server. |
| base / base.en | 142 MB | 2–4 | Good default for a busy server. |
| small / small.en | 466 MB | 4–8 | Recommended when accuracy matters. Limit concurrency by core count. |
| medium | 1.5 GB | 8+ | Single stream on a dedicated machine only. |
| large | 3 GB+ | — | Not recommended; it cannot keep up with live audio. |

The numbers above target roughly twice real-time speed, which leaves headroom for the transcoding
that shares the same CPU. Multilingual models are somewhat slower than their `.en` counterparts,
and a server CPU with AVX-512 or AMX is faster than the baseline build.

Set `<Modules><Whisper><MaxThreads>` to the total you are willing to spend on transcription. Active
STT tracks share that budget equally and the share is recomputed as tracks start and stop, so a
track never keeps threads another one needs. Each track always gets at least one thread; a track
that gets less than its `<Threads>` logs a warning.

If a model turns out to be too large for the machine, OME logs:

```
Whisper inference is slower than real time (2480 ms for a 2000 ms step) and subtitles will fall
behind. Use a smaller model, raise <Threads>, or reduce the number of concurrent STT tracks.
```

Memory is checked before each model and each per-stream state is allocated — inside a container,
against the cgroup limit rather than the host total. A model needs roughly twice its file size in RAM.

## Runtime Control via REST API

STT can be paused and resumed at runtime without restarting the server or recreating the stream. This is useful for temporarily disabling transcription for a specific stream (e.g., during ad breaks or when the stream is not speech-heavy) to save CPU resources.

For full API reference including request/response details and error codes, see [STT Control](../rest-api/v1/virtualhost/application/stream/stt-control.md).

| Endpoint | Description |
|---|---|
| `POST :enableStt` | Resume STT inference for the stream |
| `POST :disableStt` | Pause STT inference, dropping audio frames without running inference |
| `POST :sttStatus` | Get current enabled state and per-rendition configuration |

### Disabling STT at Startup

STT can be started in the disabled (paused) state by setting `<Enable>false</Enable>` inside the `<STT>` block. In this case, no inference runs and no memory or threads are held until the stream receives an `:enableStt` call.

```xml
<STT>
    <Enable>false</Enable>
    <Rendition>
        ...
    </Rendition>
</STT>
```

