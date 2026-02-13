# ABR and Transcoding

OvenMediaEngine supports Live Transcoding for Adaptive Bitrate(ABR) streaming and protocol compatibility.  Each protocol supports different codecs, and ABR needs to change resolution and bitrate in different ways.  Using **OutputProfile**, codecs, resolutions, and bitrates can be converted, and ABR can be configured as a variety of sets using a **Playlist**.

This document explains how to configure encoding settings, set up playlists.

<figure><img src="../.gitbook/assets/image (66).png" alt=""><figcaption></figcaption></figure>

<p align="center"><sub><mark style="color:$info;">Transcoding and Adaptive Streaming Architecture</mark></sub></p>

### Transcoding

This section explains how to define output streams, change the codec, bitrate, resolution, frame rate, sample rate, and channels for video/audio, as well as how to use the bypass method.

{% content-ref url="transcoding.md" %}
[transcoding.md](transcoding.md)
{% endcontent-ref %}

### Adaptive Bitrate (ABR) Stream

This section explains how to use a Playlist to assemble ABR streams by selecting tracks encoded in various qualities.

{% content-ref url="abr.md" %}
[abr.md](abr.md)
{% endcontent-ref %}

### TranscodeWebhook

The transcoding webhook feature is used when dynamic changes to encoding and ABR configuration are needed based on the type or quality of the input stream.

{% content-ref url="transcodewebhook.md" %}
[transcodewebhook.md](transcodewebhook.md)
{% endcontent-ref %}

### Support Codecs

These are the types of supported decoding and encoding codecs.

{% tabs %}
{% tab title="Decoding Codecs" %}
**Video**&#x20;

* VP8, H.264, H.265

**Audio**&#x20;

* AAC, Opus, MP3
{% endtab %}

{% tab title="Encoding Codecs" %}
**Video**

* VP8, H.264, H.265

**Audio**

* AAC, Opus

**Image**&#x20;

* &#x20;Jpeg, Png, WebP
{% endtab %}
{% endtabs %}

### **Hardware accelerators**

These are the types of hardware accelerators officially supported.

* NVIDIA GPU
* Xilinx Alveo U30 MA <sub><mark style="color:blue;">enterpise only<mark style="color:blue;"></sub>
* NILOGAN <sub><mark style="color:blue;">experiment<mark style="color:blue;"></sub>
* Quick Sync Video <sub><mark style="color:orange;">deprecated<mark style="color:orange;"></sub>



