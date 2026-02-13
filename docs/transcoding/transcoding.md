# Transcoding

## OutputProfile

The `<OutputProfile>` setting allows incoming streams to be re-encoded via the `<Encodes>` setting to create a new output stream. The name of the new output stream is determined by the rules set in `<OutputStreamName>`, and the newly created stream can be used according to the streaming URL format.

```markup
<OutputProfiles>
    <!-- 
    Common setting for decoders. Decodes is optional.

    <Decodes>
	Number of threads for the decoder.
	<ThreadCount>2</ThreadCount>

	By default, OME decodes all video frames. If OnlyKeyframes is true, only the keyframes will be decoded, massively improving thumbnail performance at the cost of having less control over when exactly they are generated
	<OnlyKeyframes>false</OnlyKeyframes>
    </Decodes>
    -->

    <OutputProfile>
        <Name>bypass_stream</Name>
        <OutputStreamName>${OriginStreamName}_bypass</OutputStreamName>
        <Encodes>
            <Video>
                <Bypass>true</Bypass>
            </Video>
            <Audio>
                <Name>aac_audio</Name>
                <Codec>aac</Codec>
                <Bitrate>128000</Bitrate>
                <Samplerate>48000</Samplerate>
                <Channel>2</Channel>
                <BypassIfMatch>
                    <Codec>eq</Codec>
                </BypassIfMatch>
            </Audio>
            <Audio>
                <Name>opus_audio</Name>
                <Codec>opus</Codec>
                <Bitrate>128000</Bitrate>
                <Samplerate>48000</Samplerate>
                <Channel>2</Channel>
                <BypassIfMatch>
                    <Codec>eq</Codec>
                </BypassIfMatch>
            </Audio>
        </Encodes>
    </OutputProfile>
</OutputProfiles>
```

According to the above setting, if the incoming stream name is `stream`, the output stream becomes `stream_bypass`and the stream URL can be used as follows.

* **`WebRTC`** ws://192.168.0.1:3333/app/<mark style="background-color:blue;">stream\_bypass</mark>
* **`LLHLS`** http://192.168.0.1:8080/app/<mark style="background-color:blue;">stream\_bypass</mark>/llhls.m3u8
* **`HLS`** http://192.168.0.1:8080/app/<mark style="background-color:blue;">stream\_bypass</mark>/ts:playlist.m3u8

## Encodes

### Video

You can set the video profile as below:

```markup
<Encodes>
    <Video>
        <Name>h264_hd</Name>
        <Codec>h264</Codec>
        <Width>1280</Width>
        <Height>720</Height>
        <Bitrate>2000000</Bitrate>
        <Framerate>30.0</Framerate>
        <KeyFrameInterval>30</KeyFrameInterval>
        <BFrames>0</BFrames>
<!--
        <Preset>fast</Preset>
        <ThreadCount>4</ThreadCount>
        <Lookahead>5</Lookahead>
        <Modules>x264</Modules>
-->
    </Video>
</Encodes>
```

<table><thead><tr><th width="238">Property</th><th>Description</th></tr></thead><tbody><tr><td>Codec<mark style="color:red;">*</mark></td><td>Type of codec to be encoded<br><mark style="color:blue;">See the table below</mark></td></tr><tr><td>Bitrate<mark style="color:red;">*</mark></td><td>Bit per second</td></tr><tr><td>Name<mark style="color:red;">*</mark></td><td>Encode name for Renditions<br><mark style="color:blue;">No duplicates allowed</mark></td></tr><tr><td>Width</td><td>Width of resolution</td></tr><tr><td>Height</td><td>Height of resolution</td></tr><tr><td>Framerate</td><td>Frames per second</td></tr><tr><td>KeyFrameInterval</td><td>Number of frames between two keyframes (0~600)<br><mark style="color:blue;">default is framerate (i.e. 1 second)</mark></td></tr><tr><td>BFrames</td><td>Number of B-frames (0~16)<br><mark style="color:blue;">default is 0</mark></td></tr><tr><td>Profile</td><td>H264 only encoding profile (baseline, main, high)</td></tr><tr><td>Preset</td><td>Presets of encoding quality and performance<br><mark style="color:blue;">See the table below</mark></td></tr><tr><td>ThreadCount</td><td>Number of threads in encoding</td></tr><tr><td>Lookahead</td><td><p>Number of frames to look ahead <br><mark style="color:blue;">default is 0</mark><br><mark style="color:blue;">x264 is 0-250</mark></p><p><mark style="color:blue;">nvenc is 0-31</mark><br><mark style="color:blue;">xma is 0-20</mark></p></td></tr><tr><td>Modules</td><td>An encoder library can be specified; otherwise, the default codec <mark style="color:blue;">See the table below</mark></td></tr></tbody></table>

<mark style="color:red;">\*</mark> required

#### _**Supported Video Codecs**_

<table><thead><tr><th width="116">Type</th><th width="208.33333333333331">Description</th><th width="142">Codec</th><th>Modules</th></tr></thead><tbody><tr><td>Video</td><td>VP8</td><td>vp8</td><td>SW: libvpx<mark style="color:red;">*</mark> </td></tr><tr><td></td><td>H.264 </td><td>h264</td><td><p>SW: openh264<mark style="color:red;">*</mark>,  x264</p><p>HW: nv, xma</p></td></tr><tr><td></td><td>H.265 <br>(Hardware Only)</td><td>h265</td><td>HW: nv, xma </td></tr></tbody></table>

#### _**Preset**_

A table in which presets provided for each codec library are mapped to OvenMediaEngine presets. Slow presets are of good quality and use a lot of resources, whereas Fast presets have lower quality and better performance. It can be set according to your own system environment and service purpose.

<table><thead><tr><th width="133">Presets</th><th width="138">openh264</th><th width="173">h264_nvenc</th><th width="122">vp8</th></tr></thead><tbody><tr><td><strong>slower</strong></td><td>QP( 10-39)</td><td>p7</td><td>best</td></tr><tr><td><strong>slow</strong></td><td>QP (16-45)</td><td>p6</td><td>best</td></tr><tr><td><strong>medium</strong></td><td>QP (24-51)</td><td>p5</td><td>good</td></tr><tr><td><strong>fast</strong></td><td>QP (32-51)</td><td>p4</td><td>realtime</td></tr><tr><td><strong>faster</strong></td><td>QP (40-51)</td><td>p3</td><td>realtime</td></tr></tbody></table>

_References_

* https://trac.ffmpeg.org/wiki/Encode/VP8
* https://docs.nvidia.com/video-technologies/video-codec-sdk/nvenc-preset-migration-guide/



### Audio

You can set the audio profile as below:

```markup
<Encodes>
    <Audio>
        <Name>opus_128</Name>
        <Codec>opus</Codec>
        <Bitrate>128000</Bitrate>
        <Samplerate>48000</Samplerate>
        <Channel>2</Channel>
    </Audio>
</Encodes>
```

<table><thead><tr><th width="154">Property</th><th>Description</th></tr></thead><tbody><tr><td>Codec<mark style="color:red;">*</mark></td><td>Type of codec to be encoded<br><mark style="color:blue;">See the table below</mark></td></tr><tr><td>Bitrate<mark style="color:red;">*</mark></td><td>Bits per second</td></tr><tr><td>Name<mark style="color:red;">*</mark></td><td>Encode name for Renditions<br><mark style="color:blue;">No duplicates allowed</mark></td></tr><tr><td>Samplerate</td><td>Samples per second</td></tr><tr><td>Channel</td><td>The number of audio channels</td></tr><tr><td>Modules</td><td>An encoder library can be specified; otherwise, the default codec <br><mark style="color:blue;">See the table below</mark></td></tr></tbody></table>

<mark style="color:red;">\*</mark> required

It is possible to have an audio only output profile by specifying the Audio profile and omitting a Video one.

#### **Supported Audio Codecs**

<table><thead><tr><th width="116">Type</th><th width="165.33333333333331">Description</th><th width="214">Codec</th><th>Modules</th></tr></thead><tbody><tr><td>Audio</td><td>AAC</td><td>aac</td><td>SW: fdkaac<mark style="color:red;">*</mark></td></tr><tr><td></td><td>Opus</td><td>opus</td><td>SW: libopus<mark style="color:red;">*</mark></td></tr></tbody></table>



### Bypass (Passthrough)

You can configure Video and Audio to bypass transcoding as follows:

```markup
<Video>
    <Bypass>true</Bypass>
</Video>
<Audio>
    <Bypass>true</Bypass>
</Audio>
```

{% hint style="warning" %}
You need to consider codec compatibility with some browsers. For example, chrome only supports OPUS codec for audio to play WebRTC stream. If you set to bypass incoming audio, it can't play on chrome.
{% endhint %}

WebRTC doesn't support AAC, so if video bypasses transcoding, audio must be encoded in OPUS.

```markup
<Encodes>
    <Video>
        <Bypass>true</Bypass>
    </Video>
    <Audio>
        <Codec>opus</Codec>
        <Bitrate>128000</Bitrate>
        <Samplerate>48000</Samplerate>
        <Channel>2</Channel>
    </Audio>
</Encodes>
```



## Additional Features

### **Conditional Encoding**

If the codec or quality of the input stream is the same as the profile to be encoded into the output stream. there is no need to perform re-transcoding while unnecessarily consuming a lot of system resources. If the quality of the input track matches all the conditions of **BypassIfMatch**, it will be Pass-through without encoding

#### Matching elements in video

<table><thead><tr><th width="206">Elements</th><th width="166">Condition</th><th>Description</th></tr></thead><tbody><tr><td>Codec <em><mark style="color:blue;">(Optional)</mark></em></td><td>eq</td><td>Compare video codecs</td></tr><tr><td>Width <em><mark style="color:blue;">(Optional)</mark></em></td><td>eq, lte, gte</td><td>Compare horizontal pixel of video resolution</td></tr><tr><td>Height <em><mark style="color:blue;">(Optional)</mark></em></td><td>eq, lte, gte</td><td>Compare vertical pixel of video resolution</td></tr><tr><td>SAR <em><mark style="color:blue;">(Optional)</mark></em></td><td>eq</td><td>Compare ratio of video resolution</td></tr></tbody></table>

\* **eq**: equal to / **lte**: less than or equal to / **gte**: greater than or equal to

#### Matching elements in audio

<table><thead><tr><th width="214">Elements</th><th width="162">Condition</th><th>Description</th></tr></thead><tbody><tr><td>Codec <em><mark style="color:blue;">(Optional)</mark></em></td><td>eq</td><td>Compare audio codecs</td></tr><tr><td>Samplerate <em><mark style="color:blue;">(Optional)</mark></em></td><td>eq, lte, gte</td><td>Compare sampling rate of audio</td></tr><tr><td>Channel <em><mark style="color:blue;">(Optional)</mark></em></td><td>eq, lte, gte</td><td>Compare number of channels in audio</td></tr></tbody></table>

\* **eq**: equal to / **lte**: less than or equal to / **gte**: greater than or equal to

To support WebRTC and LLHLS, AAC and Opus codecs must be supported at the same time. Use the settings below to reduce unnecessary audio encoding.

```xml
<Encodes>
	<Video>
                <Bypass>true</Bypass>	
	</Video>
	<Audio>
		<Name>cond_audio_aac</Name>
		<Codec>aac</Codec>
		<Bitrate>128000</Bitrate>
		<Samplerate>48000</Samplerate>
		<Channel>2</Channel>
		<BypassIfMatch>
			<Codec>eq</Codec>
			<Samplerate>lte</Samplerate>
			<Channel>eq</Channel>			
		</BypassIfMatch>
	</Audio>		
	<Audio>
		<Name>cond_audio_opus</Name>
		<Codec>opus</Codec>
		<Bitrate>128000</Bitrate>
		<Samplerate>48000</Samplerate>
		<Channel>2</Channel>
		<BypassIfMatch>
			<Codec>eq</Codec>
			<Samplerate>lte</Samplerate>
			<Channel>eq</Channel>	
		</BypassIfMatch>	
	</Audio>
</Encodes>
```

If a video track with a lower quality than the encoding option is input, unnecessary upscaling can be prevented. **SAR (Storage Aspect Ratio)** is the ratio of original pixels. In the example below, even if the width and height of the original video are smaller than or equal to the width and height set in the encoding option, if the ratio is different, it means that encoding is performed without bypassing.

```xml
<Encodes>
	<Video>
		<Name>prevent_upscaling_video</Name>
		<Codec>h264</Codec>
		<Bitrate>2048000</Bitrate>
		<Width>1280</Width>
		<Height>720</Height>
		<Framerate>30</Framerate>
		<BypassIfMatch>
			<Codec>eq</Codec>
			<Width>lte</Width>
			<Height>lte</Height>
			<SAR>eq</SAR>
		</BypassIfMatch>
	</Video>
</Encodes>
```



### **Keep Original and Change Bitrate**

If you want to transcode with the same quality as the original. See the sample below for possible parameters that OME supports to keep original. If you remove the **Width**, **Height**, **Framerate**, **Samplerate**, and **Channel** parameters. then, It is transcoded with the same options as the original.

```xml
<Encodes>
    <Video>
        <Codec>vp8</Codec>
        <Bitrate>2000000</Bitrate>
    </Video>
     <Audio>
        <Codec>opus</Codec>
        <Bitrate>128000</Bitrate>
    </Audio>  
</Encodes>
```



### Rescaling with Keep the Aspect Ratio

To change the video resolution when transcoding, use the values of width and height in the Video encode option. If you don't know the resolution of the original, it will be difficult to keep the aspect ratio after transcoding. Please use the following methods to solve these problems. For example, if you input only the **Width** value in the Video encoding option, the **Height** value is automatically generated according to the ratio of the original video.

```xml
<Encodes>
    <Video>
        <Codec>h264</Codec>
        <Bitrate>2000000</Bitrate>
        <Width>1280</Width>
        <!-- Height is automatically calculated as the original video ratio -->
        <Framerate>30.0</Framerate>
    </Video>
    <Video>
        <Codec>h264</Codec>
        <Bitrate>2000000</Bitrate>
        <!-- Width is automatically calculated as the original video ratio -->
        <Height>720</Height>
        <Framerate>30.0</Framerate>
    </Video>    
</Encodes>
```



### Decoding Thread Configuration

The software decoder uses 2 threads by default. If the CPU speed is too low for decoding, increasing the thread count can improve performance.

```xml
<OutputProfiles>
    <!-- 
    Common setting for decoders. Decodes is optional.
    -->
    <Decodes>
	<!-- Number of threads for the decoder.-->
	<ThreadCount>2</ThreadCount>
    </Decodes>

    <OutputProfile>
    ....
    </OutputProfile>
</OutputProfiles>
```
