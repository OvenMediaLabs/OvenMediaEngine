# OvenMediaEngine

[![GitHub release](https://img.shields.io/github/v/release/OvenMediaLabs/OvenMediaEngine?color=blue)](https://github.com/OvenMediaLabs/OvenMediaEngine/releases)
[![License](https://img.shields.io/badge/license-AGPL--3.0-orange)](LICENSE)
[![Docker Build](https://img.shields.io/github/actions/workflow/status/OvenMediaLabs/OvenMediaEngine/docker-image-release-multi.yml?label=docker%20build)](https://github.com/OvenMediaLabs/OvenMediaEngine/actions/workflows/docker-image-release-multi.yml)
[![Docs Build](https://img.shields.io/github/actions/workflow/status/OvenMediaLabs/OvenMediaEngine/check-docs-build.yml?label=docs%20build)](https://github.com/OvenMediaLabs/OvenMediaEngine/actions/workflows/check-docs-build.yml)

[![Docker Pulls (current)](https://img.shields.io/docker/pulls/ovenmedialabs/ovenmediaengine?label=docker%20pulls%20(current)&color=blue)](https://hub.docker.com/r/ovenmedialabs/ovenmediaengine)
[![Docker Pulls (legacy)](https://img.shields.io/docker/pulls/airensoft/ovenmediaengine?label=docker%20pulls%20(legacy)&color=lightgrey)](https://hub.docker.com/r/airensoft/ovenmediaengine)

## What is OvenMediaEngine?
<img src="dist/OME_LLHLS_220610.svg" style="max-width: 100%; height: auto;">

OvenMediaEngine (OME) is a Sub-Second Latency Streaming Server that can stream Large-scale and High-definition live streams over Low Latency HLS (LLHLS) and WebRTC to hundreds of thousands of viewers.

OME can ingest live streams over WebRTC, SRT, RTMP, RTSP, and MPEG2-TS protocols, encode them to ABR with the embedded live transcoder, and stream them to viewers over LLHLS and WebRTC.

With OvenMediaEngine, you can easily build a powerful, sub-second latency media service.

## Demo https://space.ovenplayer.com
<img src="dist/05_OvenSpace_230214.png" style="max-width: 100%; height: auto;">

OvenSpace is a sub-second latency streaming demo service using [OvenMediaEngine](https://github.com/OvenMediaLabs/OvenMediaEngine), [OvenPlayer](https://github.com/OvenMediaLabs/OvenPlayer) and [OvenLiveKit](https://github.com/OvenMediaLabs/OvenLiveKit-Web). You can experience OvenMediaEngine live at the **[OvenSpace Demo](https://space.ovenplayer.com/)** and browse implementation examples in the [OvenSpace Repository](https://github.com/OvenMediaLabs/OvenSpace).

## Features
* Ingest
  * Push: WebRTC, WHIP(Simulcast), SRT, RTMP, E-RTMP, MPEG-2 TS/UDP
  * Pull: RTSP, OVT
  * Scheduled Channel (Pre-recorded Live)
  * Multiplex Channel (Duplicate stream / Mux tracks)
* Adaptive Bitrate Streaming (ABR) for LLHLS and WebRTC
* Low Latency Streaming using LLHLS
  * DVR (Live Rewind)
  * Dump for VoD
  * ID3v2 timed metadata
  * DRM (Widevine, Fairplay)
  * Subtitle (WebVTT)
* Sub-Second Latency Streaming using WebRTC
  * WebRTC over TCP (With Embedded TURN Server)
  * Embedded WebRTC Signalling Server (WebSocket based)
  * Retransmission with NACK
  * ULPFEC (Uneven Level Protection Forward Error Correction)
    * <i>VP8, H.264, H.265</i>
  * In-band FEC (Forward Error Correction)
    * <i>Opus</i>
* Legacy HLS (HLS version 3)
  * Dump for VoD
  * MPEG-2 TS Container
  * Audio/Video Muxed
  * DVR
* Sub-Second Latency Streaming using SRT
  * Secure Reliable Transport
  * MPEG-2 TS Container
  * Audio/Video Muxed
* Embedded Live Transcoder
  * Video: VP8, H.264, H.265(Hardware only), Pass-through
  * Audio: Opus, AAC, Pass-through
* Clustering (Origin-Edge Structure)
* Monitoring
* Access Control
  * Admission Webhooks
  * Signed Policy
* File Recording
* Push Publishing using SRT, RTMP and MPEG2-TS (Re-streaming)
* Thumbnail
* REST API

## Supported Platforms
Although we have tested OvenMediaEngine on the platforms listed below, it may work with other Linux packages as well:

* [Docker](https://hub.docker.com/r/ovenmedialabs/ovenmediaengine)
* Ubuntu 18+
* Rocky Linux 8+
* AlmaLinux 8+
* Fedora 28+

## Quick Start

* [Quick Start Guide](https://docs.ovenmediaengine.com/quick-start)
* [Manual](https://docs.ovenmediaengine.com/)

### Docker
```bash
docker run --name ome -d -e OME_HOST_IP=Your.HOST.IP.Address \
-p 1935:1935 -p 9999:9999/udp -p 9000:9000 -p 3333:3333 -p 3478:3478 -p 10000-10003:10000-10003/udp -p 10000:10000/tcp \
ovenmedialabs/ovenmediaengine:latest
```

You can also store the configuration files on your host:

```bash
docker run --name ome -d -e OME_HOST_IP=Your.HOST.IP.Address \
-p 1935:1935 -p 9999:9999/udp -p 9000:9000 -p 3333:3333 -p 3478:3478 -p 10000-10003:10000-10003/udp -p 10000:10000/tcp \
-v ome-origin-conf:/opt/ovenmediaengine/bin/origin_conf \
-v ome-edge-conf:/opt/ovenmediaengine/bin/edge_conf \
ovenmedialabs/ovenmediaengine:latest
```

The configuration files are now accessible under `/var/lib/docker/volumes/<volume_name>/_data`.

Following the above example, you will find them under `/var/lib/docker/volumes/ome-origin-conf/_data` and `/var/lib/docker/volumes/ome-edge-conf/_data`.

If you want to put them in a different location, the easiest way is to create a link:
```bash
ln -s /var/lib/docker/volumes/ome-origin-conf/_data/ /my/new/path/to/ome-origin-conf \
&& ln -s /var/lib/docker/volumes/ome-edge-conf/_data/ /my/new/path/to/ome-edge-conf
```

Please read the [Getting Started](https://docs.ovenmediaengine.com/getting-started) for more information.

### WebRTC Live Encoder for Testing
* https://demo.ovenplayer.com/demo_input.html

### Player for Testing
* Without TLS: http://demo.ovenplayer.com
* With TLS: https://demo.ovenplayer.com

## How to Contribute
Thank you for your interest in contributing to OvenMediaEngine.

We need your help to keep this project growing. There are many ways to contribute.
For more information on how to contribute, please see our [Guidelines](CONTRIBUTING.md) and [Rules](CODE_OF_CONDUCT.md).

- [Finding Bugs](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#finding-bugs)
- [Reviewing Code](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#reviewing-code)
- [Sharing Ideas](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#sharing-ideas)
- [Testing](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#testing)
- [Improving Documentation](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#improving-documentation)
- [Spreading & Use Cases](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#spreading--use-cases)
- [Recurring Donations](https://github.com/OvenMediaLabs/OvenMediaEngine/blob/master/CONTRIBUTING.md#recurring-donations)

We hope OvenMediaEngine inspires you to build something great.

## About OvenMedia Labs and Projects
OvenMedia Labs aims to make it easier for you to build a stable broadcasting/streaming service with Sub-Second Latency.
Therefore, we will continue developing and providing the most optimized tools for smooth Sub-Second Latency Streaming.

* Information
  * [OvenMedia Labs Website](https://ovenmedialabs.com): About OvenMediaEngine, Enterprise on Marketplace, and more
  * [OvenMedia Labs' Blog](https://ovenmedialabs.com/blog): A blog researched and written directly by an OvenMediaEngine developer
* Open-Source Repository
  * [OvenMediaEngine GitHub](https://github.com/OvenMediaLabs/OvenMediaEngine): Sub-Second Latency Live Streaming Server
  * [OvenMediaEngine Docker Hub](https://hub.docker.com/r/ovenmedialabs/ovenmediaengine): Docker image for quick deployment
  * [OvenPlayer GitHub](https://github.com/OvenMediaLabs/OvenPlayer): JavaScript-based WebRTC and LLHLS Player for OvenMediaEngine
  * [OvenLiveKit GitHub](https://github.com/OvenMediaLabs/OvenLiveKit-Web): JavaScript-based Live Streaming Encoder for OvenMediaEngine
* Documentation
  * [OvenMediaEngine Getting Started](https://docs.ovenmediaengine.com): Configuration, ABR, Clustering, and more
  * [OvenPlayer Getting Started](https://docs.ovenplayer.com): UI Customization, API Reference, Examples, and more
* Demo
  * [OvenSpace Demo](https://space.ovenplayer.com): Sub-Second Latency Streaming Demo Service
  * [OvenPlayer Demo with TLS](https://demo.ovenplayer.com): For testing playback with HTTPS and WSS endpoints
  * [OvenPlayer Demo without TLS](http://demo.ovenplayer.com): For testing playback with HTTP and WS endpoints

## License
OvenMediaEngine is licensed under the [AGPL-3.0-only](LICENSE).
However, if you need another license, please feel free to email us at [contact@ovenmedialabs.com](mailto:contact@ovenmedialabs.com).
