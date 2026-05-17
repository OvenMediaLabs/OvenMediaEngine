---
slug: /
title: Introduction
sidebar_position: 1
---

## What is OvenMediaEngine?

[**OvenMediaEngine**](https://github.com/OvenMediaLabs/OvenMediaEngine) (OME) is a **Sub**-**Second Latency Live Streaming Server** with **Large**-**Scale** and **High**-**Definition**. With OME, you can create platforms/services/systems that transmit high-definition video to hundreds-thousand viewers with sub-second latency and be scalable, depending on the number of concurrent viewers.

![](./images/intro-llhls-overview.png)

OvenMediaEngine can receive a video/audio, video, or audio source from encoders and cameras such as [OvenLiveKit](https://www.ovenmediaengine.com/olk), OBS, XSplit, and more, to WebRTC, SRT, RTMP, MPEG-2 TS, and RTSP as Input. Then, OME transmits this source using **LLHLS** (Low Latency HLS) and **WebRTC** as output. Also, we provide [OvenPlayer](https://github.com/OvenMediaLabs/OvenPlayer), an Open-Source and JavaScript-based WebRTC/LLHLS Player for OvenMediaEngine.

Our goal is to make it easier for you to build a stable broadcasting/streaming service with sub-second latency.

## Features

* **Ingest**
  * Push: WebRTC, WHIP (Simulcast), SRT, RTMP, E-RTMP, MPEG-2 TS
  * Pull: RTSP
  * Scheduled Channel (Pre-recorded Live)
  * Multiplex Channel (Duplicate stream / Mux tracks)
* **Adaptive Bitrate Streaming (ABR) for HLS, LLHLS and WebRTC**
* **Low-Latency Streaming using LLHLS**
  * DVR (Live Rewind)
  * Dump for VoD
  * ID3v2 timed metadata
  * DRM (Widevine, Fairplay)
* **Sub-Second Latency Streaming using WebRTC**
  * WebRTC over TCP (with embedded TURN server)
  * Embedded WebRTC Signaling Server (WebSocket based)
  * Retransmission with NACK
  * ULPFEC (Uneven Level Protection Forward Error Correction)
    * _VP8, H.264, H.265_
  * In-band FEC (Forward Error Correction)
    * _Opus_
* **HLS (version 3) Streaming support for legacy devices**
  * MPEG-2 TS Container
  * Audio/Video Muxed
  * DVR (Live Rewind)
* **Sub-Second Latency Streaming using SRT**
  * Secure Reliable Transport
  * MPEG-2 TS Container
  * Audio/Video Muxed
* **Enhanced RTMP (E-RTMP) for Advanced Codec Support**
  * _H.264, H.265, AAC_
  * More codec support will be added continuously
* **Embedded Live Transcoder**
  * Video: VP8, H.264, H.265 (Hardware only), Pass-through
  * Audio: Opus, AAC, Pass-through
* **Clustering** (Origin-Edge Structure)
* **Monitoring**
* **Access Control**
  * AdmissionWebhooks
  * SignedPolicy
* **File Recording**
* **Push Publishing using SRT, RTMP and MPEG2-TS** (Re-streaming)
* **Thumbnail**
* **REST API**

## Supported Platforms

We have tested OvenMediaEngine on platforms, listed below. However, we think it can work with other Linux packages as well:

* Docker ([https://hub.docker.com/r/ovenmedialabs/ovenmediaengine](https://hub.docker.com/r/ovenmedialabs/ovenmediaengine))
* Ubuntu 18+
* Rocky Linux 8+
* AlmaLinux 8+
* Fedora 28+

## Getting Started

Please read [Getting Started](getting-started/README.md) chapter in the tutorials.

## How to Contribute

Thank you so much for being so interested in OvenMediaEngine.

We need your help to keep and develop our open-source project, and we want to tell you that you can contribute in many ways. Please see our [Guidelines](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md), [Rules](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CODE_OF_CONDUCT.md), and [Contribute](https://www.ovenmediaengine.com/contribute).

* [Finding Bugs](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#finding-bugs)
* [Reviewing Code](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#reviewing-code)
* [Sharing Ideas](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#sharing-ideas)
* [Testing](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#testing)
* [Improving Documentation](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#improving-documentation)
* [Spreading & Use Cases](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#spreading--use-cases)
* [Recurring Donations](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#recurring-donations)

We always hope that OvenMediaEngine will give you good inspiration.

## For more information

* [OvenMediaEngine GitHub](https://github.com/OvenMediaLabs/OvenMediaEngine)
* [OvenMediaEngine Website](https://ovenmediaengine.com)
* [OvenMediaEngine Tutorial Source](https://github.com/OvenMediaLabs/OvenMediaEngineDocs)
* Test Player
  * _Without TLS:_ [_http://demo.ovenplayer.com_](http://demo.ovenplayer.com)
  * _With TLS:_ [_https://demo.ovenplayer.com_](https://demo.ovenplayer.com)
* [OvenPlayer Github](https://github.com/OvenMediaLabs/OvenPlayer)
* [OvenMedia Labs Website](https://www.ovenmedialabs.com)

## License

OvenMediaEngine is licensed under the [AGPL-3.0-only](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/LICENSE). However, if you need another license, please feel free to email us at [contact@ovenmedialabs.com](mailto:contact@ovenmedialabs.com).
