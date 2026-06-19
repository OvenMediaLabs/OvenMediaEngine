---
title: ABR and Transcoding
description: "Transcode OvenMediaEngine streams — codec, bitrate, resolution, frame rate — and build adaptive bitrate renditions."
sidebar_position: 18
---

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

OvenMediaEngine supports Live Transcoding for Adaptive Bitrate(ABR) streaming and protocol compatibility.  Each protocol supports different codecs, and ABR needs to change resolution and bitrate in different ways.  Using **OutputProfile**, codecs, resolutions, and bitrates can be converted, and ABR can be configured as a variety of sets using a **Playlist**.

This document explains how to configure encoding settings, set up playlists.

![](../images/transcoding-overview.png)

<p align="center"><sub>Transcoding and Adaptive Streaming Architecture</sub></p>

### Transcoding

This section explains how to define output streams, change the codec, bitrate, resolution, frame rate, sample rate, and channels for video/audio, as well as how to use the bypass method.


[OutputProfile](output-profile.md)


### Adaptive Bitrate (ABR) Stream

This section explains how to use a Playlist to assemble ABR streams by selecting tracks encoded in various qualities.


[abr.md](abr.md)


### TranscodeWebhook

The transcoding webhook feature is used when dynamic changes to encoding and ABR configuration are needed based on the type or quality of the input stream.


[transcodewebhook.md](transcodewebhook.md)


### Support Codecs

These are the types of supported decoding and encoding codecs.


<Tabs>
<TabItem value="decoding-codecs" label="Decoding Codecs">

<div style={{paddingLeft: '1rem'}}>

**Video**

* VP8, H.264, H.265

**Audio**

* AAC, Opus, MP3, MP2

</div>

</TabItem>
<TabItem value="encoding-codecs" label="Encoding Codecs">

<div style={{paddingLeft: '1rem'}}>

**Video**

* VP8, H.264, H.265 <sub>(Not supported, SW codec support planned)</sub>

**Audio**

* AAC, Opus

**Image**

* Jpeg, Png, WebP

</div>

</TabItem>
</Tabs>


### **Hardware accelerators**

Hardware acceleration is no longer supported in the open-source version in releases after **v0.20.5**.<br/>
For more information, see our official announcement: [Discontinuing Hardware Acceleration in OvenMediaEngine](https://ovenmedialabs.com/blog/discontinuing-hardware-acceleration-ome)





