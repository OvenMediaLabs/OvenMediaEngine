---
title: Thumbnail
description: "Publish OvenMediaEngine stream thumbnails over HTTP(S), optionally sharing the HLS/DASH port."
sidebar_position: 35
---

OvenMediaEngine can generate thumbnails from live streams. This allows you to organize a broadcast list on your website or monitor multiple streams at the same time.

## Configuration

### Bind

Thumbnails are published via HTTP(s). Set the port for thumbnails as follows. Thumbnail publisher can use the same port number as HLS and DASH.

```markup
<Bind>
    <Publishers>
      ...
        <Thumbnail>
            <Port>20080</Port>
            <!-- If you need TLS support, please uncomment below:
            <TLSPort>20081</TLSPort>
            -->
        </Thumbnail>
    </Publishers>
</Bind>
```

### Encoding

To publish thumbnails, you need to set up an encoding profile. You can choose **JPG, PNG, WEBP** and **AVIF** as the format.  You can set the Framerate and Resolution. Please refer to the sample below.

```markup
<OutputProfiles>
    <OutputProfile>
        <Name>default_stream</Name>
        <OutputStreamName>${OriginStreamName}_preview</OutputStreamName>
        <Encodes>
            <Image>
                <Codec>jpeg</Codec>
                <Framerate>1</Framerate>
                <Width>1280</Width>
                <Height>720</Height>
            </Image>
            <Image>
                <Codec>png</Codec>
                <Framerate>1</Framerate>
                <Width>1280</Width>
                <Height>720</Height>
            </Image>
            <Image>
                <Codec>webp</Codec>
                <Framerate>1</Framerate>
                <Width>1280</Width>
                <Height>720</Height>
            </Image>
            <Image>
                <Codec>avif</Codec>
                <Framerate>1</Framerate>
                <Width>1280</Width>
                <Height>720</Height>
            </Image>            
        </Encodes>
    </OutputProfile>
</OutputProfiles>
```

<table><thead><tr><th width="290">Property</th><th>Description</th></tr></thead><tbody><tr><td>Codec</td><td>Specifies the image codec to use</td></tr><tr><td>Width</td><td>Width of resolution</td></tr><tr><td>Height</td><td>Height of resolution</td></tr><tr><td>Framerate</td><td>Frames per second</td></tr><tr><td>QScale</td><td>JPEG only. mjpeg quantizer scale, 1-31. Lower is better quality; 2 (the encoder default) when not set</td></tr><tr><td>Quality</td><td>WebP only. libwebp quality factor, 0-100. Higher is better quality; 75 (the encoder default) when not set</td></tr><tr><td>ChromaSampling</td><td>JPEG and AVIF. <code>420</code> (default) or <code>444</code>. 4:4:4 keeps full chroma resolution: sharper text and line art at the cost of larger files</td></tr><tr><td>Method</td><td>WebP only. libwebp method, 0-6. Higher methods spend more CPU to fit the same quality into fewer bytes; 1 when not set</td></tr><tr><td>Lossless</td><td>WebP only. <code>true</code> encodes losslessly; Quality then controls compression effort instead of quantization. Default <code>false</code></td></tr><tr><td>Preset</td><td>WebP only. libwebp preset: <code>default</code>, <code>picture</code>, <code>photo</code>, <code>drawing</code>, <code>icon</code> or <code>text</code>. A preset overrides Method and Lossless (a warning is logged if either is set), while Quality still applies; leave unset for the fastest method at default quality. Preset meanings are described in the <a href="https://developers.google.com/speed/webp/docs/cwebp">libwebp documentation</a></td></tr><tr><td>Speed</td><td>AVIF only. libaom cpu-used, 0-8. Lower spends more CPU for better compression; 8 (fastest) when not set</td></tr><tr><td>Crf</td><td>AVIF only. libaom constant-quality factor, 0-63. Lower is better quality; 30 when not set</td></tr><tr><td>PassthroughAV1</td><td>AVIF only. When <code>true</code>, AV1 input is rewrapped into AVIF at the source resolution without transcoding, so Width/Height/Speed/Crf/ChromaSampling/Framerate are ignored for AV1; non-AV1 input is unaffected. Default <code>false</code></td></tr></tbody></table>

Invalid option values log a warning and fall back to the defaults (out-of-range numbers are clamped). Options set on a profile whose codec they do not apply to are ignored, with a warning. AVIF encoding is single-threaded: at thumbnail rates threading buys nothing.

#### AV1 sources and AVIF

When the input is AV1 and the AVIF profile sets `PassthroughAV1`, thumbnails are the source key frames rewrapped into AVIF files without transcoding, at the source resolution — so thumbnail size follows the source, and different AV1 streams produce different sizes. Width/Height/Framerate/Speed/Crf/ChromaSampling are ignored on this path, and the cadence equals the source keyframe interval — the publisher controls that, so a long GOP means stale thumbnails. The file carries the publisher's original CICP rather than the canonical BT.709 full-range conversion described below. Without `PassthroughAV1` (the default), AV1 input is transcoded to the configured resolution like any other input. This option affects AV1 ingest only.

#### Supported image codecs

<table><thead><tr><th width="149">Encode Type</th><th width="177.33333333333331">Codec</th><th>Codec of Configuration</th></tr></thead><tbody><tr><td>Image</td><td>JPEG</td><td>jpeg</td></tr><tr><td></td><td>PNG</td><td>png</td></tr><tr><td></td><td>WEBP</td><td>webp</td></tr><tr><td></td><td>AVIF</td><td>avif</td></tr></tbody></table>


:::warning

The image encoding profile is only used by thumbnail publishers. and, bypass option is not supported.

:::

#### Color handling

Thumbnails are converted to the colorimetry each format implies: BT.601 full range (JFIF) for JPEG, BT.601 limited range for WebP, and RGB for PNG. Sources tagged BT.709 or BT.601 are converted accordingly. Untagged sources are assumed BT.709. PNG thumbnails are written without color chunks (untagged like the others). Transcoded AVIF is converted to BT.709 full range and signals exactly that via CICP.

### Publisher

Declaring a thumbnail publisher. Cross-domain settings are available as a detailed option.

```markup
<Publishers>
    ...
    <Thumbnail>
        <CrossDomains>
            <Url>*</Url>
        </CrossDomains>	
    </Thumbnail>
</Publishers>
```

## Get thumbnails

When the setting is made for the thumbnail and the stream is input, you can view the thumbnail through the following URL.

| Method | URL Pattern                                                                                        |
| ------ | -------------------------------------------------------------------------------------------------- |
| GET    | http(s)://\<ome\_hos&#x74;_>:\<port>/\<app\_name>/\<output\_stream\_name>/thumb.\<jpg\|png\|webp\|avif>_ |

## Advanced&#x20;

### Keyframes Decoding Only

For use cases without video (re)encoding, OME can be set to only decode the keyframes of incoming streams. This is a massive performance increase when all you are using the encoder for is generating thumbnails.


:::info

_Supported since OvenmediaEngine version 0.17.2_

:::


```xml
<OutputProfiles>
<!-- Common setting for decoders. Decodes is optional. -->
	<Decodes>
	<!-- 
	By default, OME decodes all video frames. 
	With OnlyKeyframes, only keyframes are decoded,
	massively improving performance.
	Thumbnails are generated only on keyframes,
	they may not generate at your requested fps!
	-->
		<OnlyKeyframes>true</OnlyKeyframes>
	</Decodes>

    <OutputProfile>
       <Encodes>
           <Video>
                <Bypass>true</Bypass>	
  		   </Video>
           <Image>
               <Codec>jpeg</Codec>
               <Width>1280</Width>
               <Height>720</Height>
               <Framerate>1</Framerate>
          </Image>
       </Encodes>
    </OutputProfile>
</OutputProfiles>
```

### Encoding statistics

Per-frame timing for image tracks can be logged by raising the ThumbStat tag to debug level in Logger.xml. Each transform and encode step then logs its wall-clock and thread-CPU time in microseconds.

```xml
<Tag name="ThumbStat" level="debug" />
```

### CrossDomains

For information on CrossDomains, see [CrossDomains ](crossdomains.md)chapter.
