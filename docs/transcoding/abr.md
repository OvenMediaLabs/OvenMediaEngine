# ABR

## Adaptive Bitrate Streaming (ABR)

From version 0.14.0, OvenMediaEngine can encode same source with multiple bitrates renditions and deliver it to the player.

As shown in the example configuration below, you can provide ABR by adding `<Playlists>` to `<OutputProfile>`. There can be multiple playlists, and each playlist can be accessed with `<FileName>`.

The method to access the playlist set through LLHLS is as follows.

`http[s]://<domain>[:port]/<app>/<stream>/`**`<FileName>`**`.m3u8`

The method to access the playlist set through HLS is as follows.

`http[s]://<domain>[:port]/<app>/<stream>/`**`<FileName>`**`.m3u8?format=ts`

The method to access the Playlist set through WebRTC is as follows.

`ws[s]://<domain>[:port]/<app>/<stream>/`**`<FileName>`**

Note that `<FileName>` must never contain the **`playlist`** and **`chunklist`** keywords. This is a reserved word used inside the system.

To set up `<Rendition>`, you need to add `<Name>` to the elements of `<Encodes>`. Connect the set `<Name>` into `<Rendition><Video>` or `<Rendition><Audio>`.

In the example below, three quality renditions are provided and the URL to play the `abr` playlist as LLHLS is `https://domain:port/app/stream/abr.m3u8` and The WebRTC playback URL is `wss://domain:port/app/stream/abr`

{% hint style="info" %}
TS files used in HLS must have A/V pre-muxed, so the `EnableTsPackaging` option must be set in the Playlist.
{% endhint %}

```xml
<OutputProfile>
	<Name>bypass_stream</Name>
	<OutputStreamName>${OriginStreamName}</OutputStreamName>
	<!--LLHLS URL : https://domain/app/stream/abr.m3u8 --> 
	<Playlist>
		<Name>For LLHLS</Name>
		<FileName>abr</FileName>
		<Options> <!-- Optional -->
			<!-- 
			Automatically switch rendition in WebRTC ABR 
			[Default] : true
			-->
			<WebRtcAutoAbr>true</WebRtcAutoAbr> 
			<EnableTsPackaging>true</EnableTsPackaging>
		</Options>
		<Rendition>
			<Name>Bypass</Name>
			<Video>bypass_video</Video>
			<Audio>bypass_audio</Audio>
		</Rendition>
		<Rendition>
			<Name>FHD</Name>
			<Video>video_1280</Video>
			<Audio>bypass_audio</Audio>
		</Rendition>
		<Rendition>
			<Name>HD</Name>
			<Video>video_720</Video>
			<Audio>bypass_audio</Audio>
		</Rendition>
	</Playlist>
	<!--LLHLS URL : https://domain/app/stream/llhls.m3u8 --> 
	<Playlist>
		<Name>Change Default</Name>
		<FileName>llhls</FileName>
		<Rendition>
			<Name>HD</Name>
			<Video>video_720</Video>
			<Audio>bypass_audio</Audio>
		</Rendition>
	</Playlist> 
	<Encodes>
		<Audio>
			<Name>bypass_audio</Name>
			<Bypass>true</Bypass>
		</Audio>
		<Video>
			<Name>bypass_video</Name>
			<Bypass>true</Bypass>
		</Video>
		<Audio>
			<Codec>opus</Codec>
			<Bitrate>128000</Bitrate>
			<Samplerate>48000</Samplerate>
			<Channel>2</Channel>
		</Audio>
		<Video>
			<Name>video_1280</Name>
			<Codec>h264</Codec>
			<Bitrate>5024000</Bitrate>
			<Framerate>30</Framerate>
			<Width>1920</Width>
			<Height>1280</Height>
			<Preset>faster</Preset>
		</Video>
		<Video>
			<Name>video_720</Name>
			<Codec>h264</Codec>
			<Bitrate>2024000</Bitrate>
			<Framerate>30</Framerate>
			<Width>1280</Width>
			<Height>720</Height>
			<Preset>faster</Preset>
		</Video>
	</Encodes>
</OutputProfile>
```

## Supported codecs by streaming protocol

Even if you set up multiple codecs, there is a codec that matches each streaming protocol supported by OME, so it can automatically select and stream codecs that match the protocol. However, if you don't set a codec that matches the streaming protocol you want to use, it won't be streamed.

The following is a list of codecs that match each streaming protocol:

| Protocol | Supported Codec   |
| -------- | ----------------- |
| WebRTC   | VP8, H.264, Opus  |
| LLHLS    | H.264, H.265, AAC |

Therefore, you set it up as shown in the table. If you want to stream using LLHLS, you need to set up H.264, H.265 and AAC, and if you want to stream using WebRTC, you need to set up Opus.

Also, if you are going to use WebRTC on all platforms, you need to configure both VP8 and H.264. This is because different codecs are supported for each browser, for example, VP8 only, H264 only, or both.

However, don't worry. If you set the codecs correctly, OME automatically sends the stream of codecs requested by the browser.
